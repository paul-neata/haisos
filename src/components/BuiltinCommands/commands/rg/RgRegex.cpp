#include "RgRegex.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "src/components/Regex/Regex.h"

namespace Haisos {
namespace {

const char* const kSetOperationsMessage =
    "class set operations and nested classes are not supported by HaisosOS rg";
const char* const kUnicodeClassMessage = "Unicode classes are not supported by HaisosOS rg";
const char* const kNonAsciiClassMessage =
    "non-ASCII characters in a class are not supported by HaisosOS rg";
const char* const kInvalidRangeMessage =
    "invalid character class range, the start must be <= the end";

bool IsAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

bool IsHexDigit(char c) {
    return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

unsigned HexValue(char c) {
    if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
    return static_cast<unsigned>(c - 'A' + 10);
}

// What Rust's x flag skips, and what { m , n } always skips: the ASCII
// whitespace bytes (a '#' comment runs to the end of the line, which a
// pattern cannot hold -- the newline is refused before this).
bool IsSpaceByte(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

bool IsWordChar(char c) { return IsAlpha(c) || IsDigit(c) || c == '_'; }

std::string HexByte(unsigned char byte) {
    char hex[8];
    std::snprintf(hex, sizeof(hex), "\\x%02x", byte);
    return std::string(hex);
}

// The POSIX class names, as a member of a Haisos class. "word" and "ascii"
// are handled apart (Haisos's classes do not know them).
bool KnownPosixName(const std::string& name) {
    static const char* const names[] = {"alnum", "alpha", "ascii", "blank", "cntrl", "digit",
                                        "graph", "lower", "print", "punct", "space", "upper",
                                        "word", "xdigit"};
    return std::find(std::begin(names), std::end(names), name) != std::end(names);
}

// The flags of a group, as Rust scopes them: i, m and s pass on to the Perl
// text, u and R are dropped, x and U are this translator's business.
struct Flags {
    bool ignoreCase = false;         // i
    bool multiline = false;          // m
    bool dotAll = false;             // s
    bool ignoreWhitespace = false;  // x
    bool swapGreed = false;         // U
};

// One '(' still open: where it sits in the wrapped pattern (the innermost
// still-open one is what "unclosed group" points at), where its text began
// in m_out (what a stacked quantifier wraps), and the flags to give back
// when it closes.
struct GroupEntry {
    size_t openPos = 0;
    size_t outOpen = 0;
    Flags saved;
};

// One '[' still open. Only the outermost matters: its start and the length
// of what it scanned before its first member ('[' plus '^', x-skipped space
// and a leading literal ']') are the span of "unclosed character class".
struct ClassEntry {
    size_t start = 0;
    size_t prefixLength = 1;
};

// What a class refusal will say once its outermost ']' arrives. The first
// refusal wins; a class that never closes reports that instead.
struct Refusal {
    size_t position = 0;
    size_t length = 1;
    const char* message = nullptr;
};

// One pass over the wrapped pattern, left to right, with explicit stacks.
// Emit Perl text as you go: what m_out holds when Parse returns is the
// translation.
class Translator {
public:
    Translator(std::string_view wrapped, RgRegexError& error, bool& hasUpper)
        : m_w(wrapped), m_error(error), m_hasUpper(hasUpper) {}

    // Whether some class held nothing but '\n'. rg strips its line
    // terminator out of every class, and a class left empty matches nothing:
    // rg's multiline message, not a frame.
    bool NewlineOnlyClass() const { return m_sawNewlineOnlyClass; }

    bool Parse(std::string& out) {
        while (m_i < m_w.size()) {
            SkipFlaggedSpace();
            if (m_i >= m_w.size()) break;
            if (!ParseByte()) return false;
        }
        if (!m_classes.empty()) {
            const ClassEntry& front = m_classes.front();
            return Fail(front.start, front.prefixLength, "unclosed character class");
        }
        if (!m_groups.empty()) return Fail(m_groups.back().openPos, 1, "unclosed group");
        out = std::move(m_out);
        return true;
    }

private:
    // What the previous item was, for a '-' inside a class.
    enum PrevKind { kPrevNone, kPrevLiteral, kPrevSet };

    // ---- failure ----

    bool Fail(size_t position, size_t length, const char* message, bool hint = false) {
        m_error.position = position;
        m_error.length = length;
        m_error.message = message;
        m_error.pcre2Hint = hint;
        return false;
    }

    bool FailTwo(size_t first, size_t second, const char* message) {
        m_error.position = first;
        m_error.length = 1;
        m_error.message = message;
        m_error.hasAux = true;
        m_error.auxPosition = second;
        return false;
    }

    bool Record(size_t position, size_t length, const char* message) {
        if (m_hasRecord) return true;  // the first refusal wins
        m_hasRecord = true;
        m_record = Refusal{position, length, message};
        return true;
    }

    // ---- items and quantifiers ----

    void StartAtom() {
        m_lastItemStart = m_out.size();
        m_lastRepeatable = true;
        m_lastQuantified = false;
        m_lastItemBoundary = false;
    }

    bool EmitQuantifier(const std::string& base, bool lazy) {
        if (m_lastItemBoundary) {
            // A quantifier with a 0 minimum makes a zero-width assertion
            // optional, as Rust's regex does (^*abc matches xabc): an
            // empty alternative, the one spelling of that Haisos's Perl
            // subset takes (it will not repeat an assertion). A non-zero
            // minimum leaves the assertion itself.
            if (base == "*" || base == "?") {
                m_out.insert(m_lastItemStart, "(?:");
                m_out += "|)";
            }
            return true;
        }
        if (m_lastQuantified) {
            // stacked: wrap what the previous quantifier took first
            m_out.insert(m_lastItemStart, "(?:");
            m_out += ')';
        }
        m_out += base;
        if (lazy != m_flags.swapGreed) m_out += '?';
        m_lastQuantified = true;
        return true;
    }

    // The x flag: unescaped whitespace and '#' comments, outside and inside
    // classes alike.
    void SkipFlaggedSpace() {
        if (!m_flags.ignoreWhitespace) return;
        for (;;) {
            if (m_i >= m_w.size()) return;
            if (IsSpaceByte(m_w[m_i])) {
                ++m_i;
                continue;
            }
            if (m_w[m_i] == '#') {
                while (m_i < m_w.size() && m_w[m_i] != '\n') ++m_i;
                continue;
            }
            return;
        }
    }

    void SkipBraceSpace() {
        while (m_i < m_w.size() && IsSpaceByte(m_w[m_i])) ++m_i;
    }

    bool ParseByte() {
        const char c = m_w[m_i];
        switch (c) {
            case '|':
                ++m_i;
                m_out += '|';
                m_lastRepeatable = false;
                return true;
            case '(': return ParseGroup();
            case ')': return CloseGroup();
            case '[': return ParseClass();
            case '*': case '+': case '?': return ParseSimpleQuantifier(c);
            case '{': return ParseCountedQuantifier();
            case '\\': return ParseEscape();
            case '.': case '^': case '$':
                ++m_i;
                StartAtom();
                m_out += c;
                if (c != '.') m_lastItemBoundary = true;  // an anchor
                return true;
            default: break;
        }
        const unsigned char byte = static_cast<unsigned char>(c);
        ++m_i;
        StartAtom();
        if (byte >= 0x80) {
            // Raw non-ASCII bytes, as the bytes they are. A multi-byte
            // sequence is one atom, so a quantifier after it repeats the
            // whole code point, not its last byte.
            size_t length = 1;
            while (m_i < m_w.size()
                   && static_cast<unsigned char>(m_w[m_i]) >= 0x80
                   && static_cast<unsigned char>(m_w[m_i]) < 0xc0) {
                ++length;
                ++m_i;
            }
            if (length > 1) {
                m_out += "(?:";
                m_out.append(m_w.substr(m_i - length, length));
                m_out += ')';
            } else {
                m_out += c;
            }
        } else if (byte >= 'A' && byte <= 'Z') {
            m_hasUpper = true;
            m_out += c;
        } else if (IsAlpha(c) || IsDigit(c)) {
            m_out += c;
        } else if (byte >= 0x20 && byte != 0x7f) {
            m_out += '\\';
            m_out += c;  // punctuation and space, escaped: themselves in Perl
        } else {
            m_out += HexByte(byte);  // control bytes
        }
        return true;
    }

    // ---- groups ----

    bool ParseGroup() {
        const size_t openPos = m_i++;
        if (m_i >= m_w.size() || m_w[m_i] != '?') {
            StartAtom();
            const size_t outOpen = m_out.size();
            m_out += '(';
            m_groups.push_back(GroupEntry{openPos, outOpen, m_flags});
            m_lastRepeatable = false;
            return true;
        }
        const size_t qPos = m_i++;  // the '?'
        if (m_i >= m_w.size() || m_w[m_i] == ')') {
            // "(?" alone, or "(?)": Rust reads the '?' as a quantifier with
            // nothing before it.
            return Fail(qPos, 1, "repetition operator missing expression");
        }
        switch (m_w[m_i]) {
            case ':': {
                ++m_i;
                StartAtom();
                const size_t outOpen = m_out.size();
                m_out += "(?:";
                m_groups.push_back(GroupEntry{openPos, outOpen, m_flags});
                m_lastRepeatable = false;
                return true;
            }
            case '=': case '!':
                return Fail(openPos, 3,
                            "look-around, including look-ahead and look-behind, is not supported",
                            true);
            case '<': {
                ++m_i;
                if (m_i < m_w.size() && (m_w[m_i] == '=' || m_w[m_i] == '!')) {
                    return Fail(openPos, 4,
                                "look-around, including look-ahead and look-behind, is not supported",
                                true);
                }
                return ParseNamedGroup(openPos);
            }
            case 'P': {
                const size_t pPos = m_i++;
                if (m_i < m_w.size() && m_w[m_i] == '<') {
                    ++m_i;
                    return ParseNamedGroup(openPos);
                }
                return Fail(pPos, 1, "unrecognized flag");
            }
            default:
                return ParseFlags(openPos);
        }
    }

    // m_i at the group's name: a letter or '_' first, then word bytes plus
    // '.' '[' ']' (a name Haisos then cannot read is left for Compile).
    bool ParseNamedGroup(size_t openPos) {
        if (m_i >= m_w.size() || !(IsAlpha(m_w[m_i]) || m_w[m_i] == '_')) {
            return Fail(m_i < m_w.size() ? m_i : m_w.size() - 1, 1,
                        "invalid capture group character");
        }
        const size_t nameStart = m_i++;
        while (m_i < m_w.size()
               && (IsWordChar(m_w[m_i]) || m_w[m_i] == '.' || m_w[m_i] == '['
                   || m_w[m_i] == ']')) {
            ++m_i;
        }
        if (m_i >= m_w.size()) return Fail(openPos, 1, "unclosed group");
        if (m_w[m_i] != '>') return Fail(m_i, 1, "invalid capture group character");
        ++m_i;
        StartAtom();
        const size_t outOpen = m_out.size();
        m_out += "(?<";
        m_out.append(m_w.substr(nameStart, m_i - 1 - nameStart));
        m_out += '>';
        m_groups.push_back(GroupEntry{openPos, outOpen, m_flags});
        m_lastRepeatable = false;
        return true;
    }

    // m_i at the byte after "(?".
    bool ParseFlags(size_t openPos) {
        static const char kLetters[] = "imsxuUR";
        Flags next = m_flags;
        bool negated = false;
        bool negatedAny = false;
        size_t minusPos = 0;
        size_t firstSeen[7] = {};
        bool seen[7] = {};
        for (;;) {
            if (m_i >= m_w.size()) return Fail(openPos, 1, "unclosed group");
            const char f = m_w[m_i];
            if (f == ')' || f == ':') break;
            if (f == '-') {
                if (negated) return FailTwo(minusPos, m_i, "flag negation operator repeated");
                negated = true;
                minusPos = m_i;
                ++m_i;
                continue;
            }
            if (f == '\0') return Fail(m_i, 1, "unrecognized flag");
            const char* letter = std::strchr(kLetters, f);
            if (letter == nullptr) return Fail(m_i, 1, "unrecognized flag");
            const int index = letter - kLetters;
            if (seen[index]) return FailTwo(firstSeen[index], m_i, "duplicate flag");
            seen[index] = true;
            firstSeen[index] = m_i;
            const bool on = !negated;
            if (negated) negatedAny = true;
            switch (f) {
                case 'i': next.ignoreCase = on; break;
                case 'm': next.multiline = on; break;
                case 's': next.dotAll = on; break;
                case 'x': next.ignoreWhitespace = on; break;
                case 'U': next.swapGreed = on; break;
                default: break;  // u and R: dropped, what rg's default already is
            }
            ++m_i;
        }
        const bool scoped = m_w[m_i] == ':';
        ++m_i;
        if (scoped) {
            // the flags of everything inside, written once in full
            std::string on, off;
            if (next.ignoreCase) on += 'i'; else off += 'i';
            if (next.multiline) on += 'm'; else off += 'm';
            if (next.dotAll) on += 's'; else off += 's';
            StartAtom();
            const size_t outOpen = m_out.size();
            m_out += "(?";
            m_out += on;
            if (!off.empty()) {
                m_out += '-';
                m_out += off;
            }
            m_out += ':';
            m_groups.push_back(GroupEntry{openPos, outOpen, m_flags});
            m_flags = next;
            m_lastRepeatable = false;
            return true;
        }
        // (?flags): only a change of i, m or s is visible to Perl
        std::string turnOn, turnOff;
        if (next.ignoreCase && !m_flags.ignoreCase) turnOn += 'i';
        if (!next.ignoreCase && m_flags.ignoreCase) turnOff += 'i';
        if (next.multiline && !m_flags.multiline) turnOn += 'm';
        if (!next.multiline && m_flags.multiline) turnOff += 'm';
        if (next.dotAll && !m_flags.dotAll) turnOn += 's';
        if (!next.dotAll && m_flags.dotAll) turnOff += 's';
        if (!turnOn.empty() || !turnOff.empty()) {
            m_out += "(?";
            m_out += turnOn;
            if (!turnOff.empty()) {
                m_out += '-';
                m_out += turnOff;
            }
            m_out += ')';
        }
        m_flags = next;
        m_lastRepeatable = false;
        return true;
    }

    bool CloseGroup() {
        const size_t closePos = m_i++;
        if (m_groups.empty()) return Fail(closePos, 1, "unopened group");
        m_flags = m_groups.back().saved;
        m_lastItemStart = m_groups.back().outOpen;
        m_out += ')';
        m_groups.pop_back();
        m_lastRepeatable = true;
        m_lastQuantified = false;
        m_lastItemBoundary = false;
        return true;
    }

    // ---- quantifiers ----

    bool ParseSimpleQuantifier(char c) {
        const size_t pos = m_i++;
        if (!m_lastRepeatable) return Fail(pos, 1, "repetition operator missing expression");
        const bool lazy = m_i < m_w.size() && m_w[m_i] == '?';
        if (lazy) ++m_i;
        return EmitQuantifier(std::string(1, c), lazy);
    }

    // Reads the digits of a count; m_i where a number should start.
    bool ReadCount(uint64_t& value) {
        const size_t digitsStart = m_i;
        uint64_t v = 0;
        bool overflow = false;
        while (m_i < m_w.size() && IsDigit(m_w[m_i])) {
            const uint64_t digit = static_cast<uint64_t>(m_w[m_i] - '0');
            if (v > (0xFFFFFFFFull - digit) / 10) overflow = true;
            else v = v * 10 + digit;
            ++m_i;
        }
        if (m_i == digitsStart) {
            return Fail(m_i < m_w.size() ? m_i : m_w.size() - 1, 1,
                        "repetition quantifier expects a valid decimal");
        }
        if (overflow) return Fail(digitsStart, m_i - digitsStart, "decimal literal invalid");
        value = v;
        return true;
    }

    // m_i at the '{'. Spaces inside the braces are dropped always, and what
    // stops the counts short of '}' is "unclosed" from '{' to there. A count
    // on a zero-width assertion (\b{2}, \b{0}) is the assertion itself --
    // or, with a 0 minimum, an optional one -- since Haisos's engine takes
    // no '{m}' after one.
    bool ParseCountedQuantifier() {
        const size_t openPos = m_i++;
        if (!m_lastRepeatable) return Fail(openPos, 1, "repetition operator missing expression");
        SkipBraceSpace();
        uint64_t min = 0;
        if (!ReadCount(min)) return false;
        SkipBraceSpace();
        std::string base;
        if (m_i < m_w.size() && m_w[m_i] == '}') {
            ++m_i;
            base = "{" + std::to_string(min) + "}";
        } else if (m_i < m_w.size() && m_w[m_i] == ',') {
            ++m_i;
            SkipBraceSpace();
            bool hasMax = false;
            uint64_t max = 0;
            if (m_i < m_w.size() && IsDigit(m_w[m_i])) {
                hasMax = true;
                if (!ReadCount(max)) return false;
                SkipBraceSpace();
            }
            if (m_i >= m_w.size() || m_w[m_i] != '}') {
                return Fail(openPos, m_i - openPos, "unclosed counted repetition");
            }
            ++m_i;
            if (hasMax && min > max) {
                return Fail(openPos, m_i - openPos,
                            "invalid repetition count range, the start must be <= the end");
            }
            base = "{" + std::to_string(min) + "," + (hasMax ? std::to_string(max) : "") + "}";
        } else {
            return Fail(openPos, m_i - openPos, "unclosed counted repetition");
        }
        const bool lazy = m_i < m_w.size() && m_w[m_i] == '?';
        if (lazy) ++m_i;
        if (m_lastItemBoundary) {
            if (min == 0) {
                m_out.insert(m_lastItemStart, "(?:");
                m_out += "|)";
            }
            return true;  // a non-zero minimum: the assertion itself
        }
        return EmitQuantifier(base, lazy);
    }

    // ---- escapes outside a class ----

    bool ParseEscape() {
        const size_t escPos = m_i++;
        if (m_i >= m_w.size()) {
            // the wrapper's ')' escaped: its group never closes
            return Fail(m_groups.empty() ? 0 : m_groups.back().openPos, 1, "unclosed group");
        }
        const char e = m_w[m_i];
        if (IsDigit(e)) {
            ++m_i;
            return Fail(escPos, 2, "backreferences are not supported", true);
        }
        if (IsAlpha(e)) {
            ++m_i;
            StartAtom();
            switch (e) {
                case 'd': case 'D': case 's': case 'S': case 'w': case 'W':
                    m_out += '\\';
                    m_out += e;
                    return true;
                case 'B': case 'A': case 'z':  // assertions: no '{m}' of their own
                    m_out += '\\';
                    m_out += e;
                    m_lastItemBoundary = true;
                    return true;
                case 't': m_out += "\\t"; return true;
                case 'r': m_out += "\\r"; return true;
                case 'a': m_out += "\\a"; return true;
                case 'f': m_out += "\\f"; return true;
                case 'v':  // Rust's vertical tab; Perl's \v is a class
                    m_out += "\\x0b";
                    return true;
                case 'n':
                    m_error.multiline = true;
                    return false;
                case 'b': return ParseWordBoundary();
                case 'x': case 'u': case 'U': {
                    unsigned int value = 0;
                    if (!ReadHexValue(e, value)) return false;
                    return EmitCodePoint(value);
                }
                case 'p': case 'P': return FailUnicodeClass(escPos, e);
                default:
                    return Fail(escPos, 2, "unrecognized escape sequence");
            }
        }
        ++m_i;
        const unsigned char byte = static_cast<unsigned char>(e);
        if (byte >= 0x80) return Fail(escPos, 2, "unrecognized escape sequence");
        StartAtom();
        if (e == '<' || e == '>') {
            m_out += "\\b";  // Haisos's Perl subset has no one-sided boundary
            m_lastItemBoundary = true;
            return true;
        }
        m_out += '\\';
        m_out += e;
        return true;
    }

    // m_i past a \b. \b{start} and friends are \b; a '{' with no assertion
    // name in it is a counted repetition of the \b.
    bool ParseWordBoundary() {
        if (m_i >= m_w.size() || m_w[m_i] != '{') {
            m_out += "\\b";
            m_lastItemBoundary = true;
            return true;
        }
        const size_t bracePos = m_i;
        const size_t nameStart = bracePos + 1;
        size_t p = nameStart;
        while (p < m_w.size() && (IsAlpha(m_w[p]) || m_w[p] == '-')) ++p;
        const size_t nameLength = p - nameStart;
        if (nameLength == 0) {
            // m_i at the '{': \b{2} is the boundary, \b{0} an optional one
            m_out += "\\b";
            m_lastItemBoundary = true;
            return ParseCountedQuantifier();
        }
        const std::string name(m_w.substr(nameStart, nameLength));
        if (p >= m_w.size() || m_w[p] != '}') {
            return Fail(bracePos, 1 + nameLength,
                        "special word boundary assertion is either unclosed or contains an "
                        "invalid character");
        }
        if (name != "start" && name != "end" && name != "start-half" && name != "end-half") {
            return Fail(nameStart, nameLength,
                        "unrecognized special word boundary assertion, valid choices are: "
                        "start, end, start-half or end-half");
        }
        m_i = p + 1;
        m_out += "\\b";
        m_lastItemBoundary = true;
        return true;
    }

    // m_i past the x, u or U of an escape.
    bool ReadHexValue(char kind, unsigned int& value) {
        if (m_i < m_w.size() && m_w[m_i] == '{') {
            const size_t bracePos = m_i++;
            const size_t digitsStart = m_i;
            uint64_t v = 0;
            while (m_i < m_w.size() && IsHexDigit(m_w[m_i])) {
                if (v <= 0x1000000) v = v * 16 + HexValue(m_w[m_i]);
                ++m_i;
            }
            if (m_i == digitsStart) return Fail(bracePos, 2, "hexadecimal literal empty");
            if (m_i >= m_w.size() || m_w[m_i] != '}') {
                return Fail(m_i < m_w.size() ? m_i : m_w.size() - 1, 1,
                            "invalid hexadecimal digit");
            }
            ++m_i;
            if (v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) {
                return Fail(digitsStart, m_i - 1 - digitsStart,
                            "hexadecimal literal is not a Unicode scalar value");
            }
            value = static_cast<unsigned int>(v);
            return true;
        }
        const int width = kind == 'x' ? 2 : kind == 'u' ? 4 : 8;
        const size_t digitsStart = m_i;
        uint64_t v = 0;
        for (int k = 0; k < width; ++k) {
            if (m_i >= m_w.size() || !IsHexDigit(m_w[m_i])) {
                return Fail(m_i < m_w.size() ? m_i : m_w.size() - 1, 1,
                            "invalid hexadecimal digit");
            }
            v = v * 16 + HexValue(m_w[m_i]);
            ++m_i;
        }
        if (v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) {
            return Fail(digitsStart, width, "hexadecimal literal is not a Unicode scalar value");
        }
        value = static_cast<unsigned int>(v);
        return true;
    }

    // A code point outside a class: its bytes, one \xhh each ('\n' takes the
    // multiline message instead). Above 0x7f the bytes are one atom, so a
    // quantifier after them repeats the whole code point, not its last byte.
    bool EmitCodePoint(unsigned int value) {
        if (value == 0x0a) {
            m_error.multiline = true;
            return false;
        }
        if (value <= 0x7f) {
            m_out += HexByte(static_cast<unsigned char>(value));
            return true;
        }
        m_out += "(?:";
        if (value <= 0x7ff) {
            m_out += HexByte(static_cast<unsigned char>(0xc0 | (value >> 6)));
            m_out += HexByte(static_cast<unsigned char>(0x80 | (value & 0x3f)));
        } else if (value <= 0xffff) {
            m_out += HexByte(static_cast<unsigned char>(0xe0 | (value >> 12)));
            m_out += HexByte(static_cast<unsigned char>(0x80 | ((value >> 6) & 0x3f)));
            m_out += HexByte(static_cast<unsigned char>(0x80 | (value & 0x3f)));
        } else {
            m_out += HexByte(static_cast<unsigned char>(0xf0 | (value >> 18)));
            m_out += HexByte(static_cast<unsigned char>(0x80 | ((value >> 12) & 0x3f)));
            m_out += HexByte(static_cast<unsigned char>(0x80 | ((value >> 6) & 0x3f)));
            m_out += HexByte(static_cast<unsigned char>(0x80 | (value & 0x3f)));
        }
        m_out += ')';
        return true;
    }

    // \p / \P: the span runs through the '}' when there is one.
    bool FailUnicodeClass(size_t escPos, char kind,
                          const char* message = kUnicodeClassMessage) {
        (void)kind;
        if (m_i < m_w.size() && m_w[m_i] == '{') {
            size_t p = m_i + 1;
            while (p < m_w.size() && m_w[p] != '}') ++p;
            const size_t end = p < m_w.size() ? p + 1 : p;
            return Fail(escPos, end - escPos, message);
        }
        if (m_i < m_w.size() && IsAlpha(m_w[m_i])) return Fail(escPos, 3, message);
        return Fail(escPos, 2, message);
    }

    // ---- classes ----

    // One byte as a class member: the metacharacters escaped, control bytes
    // as \xhh, everything else itself.
    std::string ClassByte(unsigned char byte) {
        if (byte == '\\' || byte == ']' || byte == '[' || byte == '^' || byte == '-') {
            return std::string("\\") + static_cast<char>(byte);
        }
        if (byte < 0x20 || byte == 0x7f) return HexByte(byte);
        return std::string(1, static_cast<char>(byte));
    }

    bool ParseClass() {
        const size_t start = m_i++;  // the '['
        bool negated = false;
        if (m_i < m_w.size() && m_w[m_i] == '^') {
            negated = true;
            ++m_i;
        }
        SkipFlaggedSpace();
        std::string cls;
        PrevKind prevKind = kPrevNone;
        unsigned char prevByte = 0;
        size_t prevPos = 0;
        m_classAllNewline = !negated;  // a class of only '\n' matches nothing
        if (m_i < m_w.size() && m_w[m_i] == ']') {
            cls += "\\]";
            ++m_i;
            // a plain member: rg starts no range from it ([]-a] is
            // ']', '-' and 'a', not ] to a)
            prevKind = kPrevNone;
            m_classAllNewline = false;
        }
        m_classes.push_back(ClassEntry{start, m_i - start});
        m_hasRecord = false;

        while (!m_classes.empty()) {
            SkipFlaggedSpace();
            if (m_i >= m_w.size()) break;
            const char c = m_w[m_i];

            if (c == ']') {
                ++m_i;
                m_classes.pop_back();
                if (m_classes.empty()) {
                    if (m_hasRecord) {
                        return Fail(m_record.position, m_record.length, m_record.message);
                    }
                    if (!negated && m_classAllNewline) m_sawNewlineOnlyClass = true;
                    StartAtom();
                    m_out += negated ? "[^" : "[";
                    m_out += cls;
                    m_out += ']';
                    return true;
                }
                prevKind = kPrevSet;  // a nested class closed: a set item
                continue;
            }

            if (c == '[') {
                // [:name:] and [:^name:], or a nested class (refused, but
                // parsed, so its ']' does not close ours)
                const size_t itemPos = m_i;
                size_t p = m_i + 1;
                bool caret = false;
                if (p < m_w.size() && m_w[p] != ':') p = m_w.size();  // not [:...:]
                if (p < m_w.size()) {
                    ++p;  // the ':'
                    if (p < m_w.size() && m_w[p] == '^') {
                        caret = true;
                        ++p;
                    }
                }
                const size_t nameStart = p;
                while (p < m_w.size() && IsAlpha(m_w[p])) ++p;
                const bool complete = p + 1 < m_w.size() && m_w[p] == ':' && m_w[p + 1] == ']';
                const std::string name(m_w.substr(nameStart, p < m_w.size() ? p - nameStart : 0));
                if (complete && KnownPosixName(name)) {
                    m_i = p + 2;
                    m_classAllNewline = false;
                    if (name == "word") {
                        cls += caret ? "\\W" : "\\w";
                    } else if (name == "ascii") {
                        cls += caret ? "\\x80-\\xff" : "\\x00-\\x7f";
                    } else {
                        cls += "[:";
                        if (caret) cls += '^';
                        cls += name;
                        cls += ":]";
                    }
                    prevKind = kPrevSet;
                    prevPos = itemPos;
                } else {
                    Record(itemPos, 1, kSetOperationsMessage);
                    m_classes.push_back(ClassEntry{itemPos, 1});
                    ++m_i;
                    prevKind = kPrevNone;
                    m_classAllNewline = false;
                }
                continue;
            }

            if ((c == '&' || c == '~') && m_i + 1 < m_w.size() && m_w[m_i + 1] == c) {
                Record(m_i, 2, kSetOperationsMessage);
                m_i += 2;
                prevKind = kPrevNone;
                m_classAllNewline = false;
                continue;
            }

            if (c == '-') {
                if (m_i + 1 < m_w.size() && m_w[m_i + 1] == '-') {
                    Record(m_i, 2, kSetOperationsMessage);
                    m_i += 2;
                    prevKind = kPrevNone;
                    m_classAllNewline = false;
                    continue;
                }
                const size_t dashPos = m_i;
                ++m_i;
                if (prevKind == kPrevLiteral) {
                    SkipFlaggedSpace();
                    if (m_i >= m_w.size() || m_w[m_i] == ']') {
                        cls += "\\-";  // no end in sight: the dash is a member
                        prevKind = kPrevLiteral;
                        prevByte = '-';
                        prevPos = dashPos;
                        m_classAllNewline = false;
                        continue;
                    }
                    if (!ParseRangeEnd(cls, prevByte, prevPos)) return false;
                    prevKind = kPrevNone;  // a dash after a range is a member
                    continue;
                }
                if (prevKind == kPrevSet && m_i < m_w.size() && m_w[m_i] != ']') {
                    return Fail(prevPos, 2, "invalid range boundary, must be a literal");
                }
                cls += "\\-";
                prevKind = kPrevLiteral;
                prevByte = '-';
                prevPos = dashPos;
                m_classAllNewline = false;
                continue;
            }

            if (c == '\\') {
                if (!ParseClassEscape(cls, prevKind, prevByte, prevPos)) return false;
                continue;
            }

            const unsigned char byte = static_cast<unsigned char>(c);
            ++m_i;
            if (byte >= 0x80) {
                Record(m_i - 1, 1, kNonAsciiClassMessage);
                prevKind = kPrevNone;
                continue;
            }
            if (byte >= 'A' && byte <= 'Z') m_hasUpper = true;
            if (byte != 0x0a) m_classAllNewline = false;
            // a ':' opening the class would read to Haisos's engine as a
            // misplaced POSIX class; escaped, it is the literal Rust reads
            // ([:alpha:] outside a class is a class of ':', 'a', 'l', ...)
            cls += (cls.empty() && byte == ':') ? "\\:" : ClassByte(byte);
            prevKind = kPrevLiteral;
            prevByte = byte;
            prevPos = m_i - 1;
        }
        const ClassEntry& front = m_classes.front();
        return Fail(front.start, front.prefixLength, "unclosed character class");
    }

    // m_i at the end item of a range, its start |startByte| (at
    // |startItemPos|) behind a '-'.
    bool ParseRangeEnd(std::string& cls, unsigned char startByte, size_t startItemPos) {
        const char e = m_w[m_i];
        unsigned char endByte = 0;
        bool endLiteral = false;
        if (e == '\\') {
            const size_t escPos = m_i++;
            if (m_i >= m_w.size()) {
                // the end never arrives: the range is the error, through the
                // end of the pattern (rg's caret lands on the wrapper's ')')
                return Fail(startItemPos, m_w.size() - startItemPos, kInvalidRangeMessage);
            }
            const char esc = m_w[m_i++];
            if (IsDigit(esc)) return Fail(escPos, 2, "backreferences are not supported", true);
            if (esc == 'x' || esc == 'u' || esc == 'U') {
                unsigned int value = 0;
                if (!ReadHexValue(esc, value)) return false;
                if (value > 0xff) {
                    Record(escPos, m_i - escPos, kNonAsciiClassMessage);
                    m_classAllNewline = false;
                    return true;  // the refusal fires when the class closes
                }
                endByte = static_cast<unsigned char>(value);
                endLiteral = true;
            } else if (esc == 't') { endByte = 0x09; endLiteral = true; }
            else if (esc == 'n') { endByte = 0x0a; endLiteral = true; }
            else if (esc == 'r') { endByte = 0x0d; endLiteral = true; }
            else if (esc == 'a') { endByte = 0x07; endLiteral = true; }
            else if (esc == 'f') { endByte = 0x0c; endLiteral = true; }
            else if (esc == 'v') { endByte = 0x0b; endLiteral = true; }
            // a unicode class can bound no range: Rust's message, the
            // whole escape under its caret
            else if (esc == 'p' || esc == 'P') {
                return FailUnicodeClass(escPos, esc,
                                         "invalid range boundary, must be a literal");
            }
            else if (IsAlpha(esc) || static_cast<unsigned char>(esc) >= 0x80) {
                if (esc == 'd' || esc == 'D' || esc == 's' || esc == 'S' || esc == 'w'
                    || esc == 'W' || esc == 'b' || esc == 'B' || esc == 'A' || esc == 'z'
                    || esc == '<' || esc == '>') {
                    return Fail(escPos, 2, "invalid range boundary, must be a literal");
                }
                return Fail(escPos, 2, "unrecognized escape sequence");
            } else {
                endByte = static_cast<unsigned char>(esc);
                endLiteral = true;
            }
        } else if (static_cast<unsigned char>(e) >= 0x80) {
            Record(m_i, 1, kNonAsciiClassMessage);
            m_classAllNewline = false;
            ++m_i;
            return true;
        } else {
            if (e >= 'A' && e <= 'Z') m_hasUpper = true;
            endByte = static_cast<unsigned char>(e);  // a '[' included: Rust
            endLiteral = true;                        // reads it literally here
            ++m_i;
        }
        if (endLiteral) {
            if (startByte > endByte) {
                // rg's caret runs from the range's start item through its
                // end item, both whole
                return Fail(startItemPos, m_i - startItemPos, kInvalidRangeMessage);
            }
            if (startByte != 0x0a || endByte != 0x0a) m_classAllNewline = false;
            cls += HexByte(startByte);
            cls += '-';
            cls += HexByte(endByte);
        }
        return true;
    }

    // m_i at the '\' of a class member.
    bool ParseClassEscape(std::string& cls, PrevKind& prevKind, unsigned char& prevByte,
                          size_t& prevPos) {
        const size_t escPos = m_i++;
        if (m_i >= m_w.size()) return false;  // the class never closes
        const char esc = m_w[m_i++];
        if (IsDigit(esc)) return Fail(escPos, 2, "backreferences are not supported", true);
        if (IsAlpha(esc)) {
            switch (esc) {
                case 'd': case 'D': case 's': case 'S': case 'w': case 'W':
                    cls += '\\';
                    cls += esc;
                    prevKind = kPrevSet;
                    prevPos = escPos;
                    m_classAllNewline = false;
                    return true;
                case 't': prevByte = 0x09; break;
                case 'n': prevByte = 0x0a; break;
                case 'r': prevByte = 0x0d; break;
                case 'a': prevByte = 0x07; break;
                case 'f': prevByte = 0x0c; break;
                case 'v': prevByte = 0x0b; break;  // Rust's vertical tab
                case 'x': case 'u': case 'U': {
                    unsigned int value = 0;
                    if (!ReadHexValue(esc, value)) return false;
                    if (value > 0xff) {
                        Record(escPos, m_i - escPos, kNonAsciiClassMessage);
                        prevKind = kPrevNone;
                        m_classAllNewline = false;
                        return true;
                    }
                    prevByte = static_cast<unsigned char>(value);
                    break;
                }
                case 'p': case 'P': {
                    // the span runs through the '}' when there is one
                    if (m_i < m_w.size() && m_w[m_i] == '{') {
                        size_t p = m_i + 1;
                        while (p < m_w.size() && m_w[p] != '}') ++p;
                        const size_t end = p < m_w.size() ? p + 1 : p;
                        Record(escPos, end - escPos, kUnicodeClassMessage);
                    } else if (m_i < m_w.size() && IsAlpha(m_w[m_i])) {
                        Record(escPos, 3, kUnicodeClassMessage);
                    } else {
                        Record(escPos, 2, kUnicodeClassMessage);
                    }
                    prevKind = kPrevNone;
                    m_classAllNewline = false;
                    return true;
                }
                case 'b': case 'B': case 'A': case 'z':
                    return Fail(escPos, 2, "invalid escape sequence found in character class");
                default:
                    return Fail(escPos, 2, "unrecognized escape sequence");
            }
            cls += HexByte(prevByte);
            prevKind = kPrevLiteral;
            prevPos = escPos;
            if (prevByte != 0x0a) m_classAllNewline = false;
            return true;
        }
        const unsigned char byte = static_cast<unsigned char>(esc);
        if (byte >= 0x80) return Fail(escPos, 2, "unrecognized escape sequence");
        if (esc == '<' || esc == '>') {
            return Fail(escPos, 2, "invalid escape sequence found in character class");
        }
        cls += ClassByte(byte);  // escaped punctuation and space: the byte
        prevKind = kPrevLiteral;
        prevByte = byte;
        prevPos = escPos;
        if (byte != 0x0a) m_classAllNewline = false;
        return true;
    }

    // ---- state ----

    std::string_view m_w;
    size_t m_i = 0;
    std::string m_out;
    Flags m_flags;
    std::vector<GroupEntry> m_groups;
    std::vector<ClassEntry> m_classes;
    bool m_hasRecord = false;
    Refusal m_record;
    size_t m_lastItemStart = 0;
    bool m_lastRepeatable = false;
    bool m_lastQuantified = false;
    bool m_lastItemBoundary = false;  // the last item is a zero-width assertion
    bool m_classAllNewline = false;   // this class holds nothing but '\n'
    bool m_sawNewlineOnlyClass = false;
    RgRegexError& m_error;
    bool& m_hasUpper;
};

} // namespace

std::optional<std::string> TranslateRgPattern(const std::vector<std::string>& patterns,
                                              std::string& wrapped, RgRegexError& error,
                                              bool& hasUppercaseLiteral) {
    wrapped.clear();
    for (size_t i = 0; i < patterns.size(); ++i) {
        if (i > 0) wrapped += '|';
        wrapped += "(?:";
        wrapped += patterns[i];
        wrapped += ')';
    }
    error = RgRegexError{};
    hasUppercaseLiteral = false;
    if (patterns.empty()) return std::string();

    Translator translator(wrapped, error, hasUppercaseLiteral);
    std::string perl;
    if (!translator.Parse(perl)) return std::nullopt;
    if (translator.NewlineOnlyClass()) {
        // a class rg's line terminator is stripped from, left empty
        error.multiline = true;
        return std::nullopt;
    }

    // What Regex itself refuses -- a size limit -- takes the frame with the
    // caret at column 0.
    std::string compileError;
    if (!Regex::Compile(perl, RegexOptions{RegexSyntax::Perl}, compileError)) {
        error = RgRegexError{};
        error.message = compileError;
        return std::nullopt;
    }
    return perl;
}

std::string FormatRgRegexError(const std::string& wrapped, const RgRegexError& error) {
    std::string out = "rg: regex parse error:\n    ";
    out += wrapped;
    out += "\n    ";
    if (error.hasAux) {
        const size_t first = std::min(error.position, error.auxPosition);
        const size_t second = std::max(error.position, error.auxPosition);
        out.append(first, ' ');
        out += '^';
        out.append(second - first - 1, ' ');
        out += '^';
    } else {
        out.append(error.position, ' ');
        out.append(std::max<size_t>(error.length, 1), '^');
    }
    out += "\nerror: ";
    out += error.message;
    out += '\n';
    if (error.pcre2Hint) {
        out += "\nConsider enabling PCRE2 with the --pcre2 flag, which can handle backreferences\n"
               "and look-around.\n";
    }
    return out;
}

} // namespace Haisos