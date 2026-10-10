#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinText.h"

namespace Haisos {

namespace {

enum TrOption {
    kTrComplement = 1, // -c and -C
    kTrDelete,
    kTrSqueeze,
    kTrTruncate,
};

// The [:class:] names, with their members in ascending byte order.
struct TrClass {
    const char* name;
    std::string members;
};

const std::vector<TrClass>& TrClasses() {
    static const std::vector<TrClass> classes = [] {
        auto byteRange = [](int first, int last) {
            std::string out;
            for (int c = first; c <= last; ++c) {
                out += static_cast<char>(c);
            }
            return out;
        };
        std::vector<TrClass> table = {
            {"alnum", byteRange('0', '9') + byteRange('A', 'Z') + byteRange('a', 'z')},
            {"alpha", byteRange('A', 'Z') + byteRange('a', 'z')},
            {"blank", std::string("\t ")},
            {"cntrl", byteRange(0, 31) + std::string(1, '\x7f')},
            {"digit", byteRange('0', '9')},
            {"graph", byteRange(33, 126)},
            {"lower", byteRange('a', 'z')},
            {"print", byteRange(32, 126)},
            {"punct", byteRange(33, 47) + byteRange(58, 64) + byteRange(91, 96) + byteRange(123, 126)},
            {"space", std::string("\t\n\v\f\r ")},
            {"upper", byteRange('A', 'Z')},
            {"xdigit", byteRange('0', '9') + byteRange('A', 'F') + byteRange('a', 'f')},
        };
        return table;
    }();
    return classes;
}

bool TrClassMembers(const std::string& name, std::string& members) {
    for (const auto& entry : TrClasses()) {
        if (name == entry.name) {
            members = entry.members;
            return true;
        }
    }
    return false;
}

bool TrClassIsCase(const std::string& name) {
    return name == "upper" || name == "lower";
}

// One element of a SET as written: a byte repeated (plain, ranged -- the
// parser expands a range to one byte element per byte -- or [c*n]), a
// character class, an equivalence class, or string2's [c*] fill.
enum class TrElemKind { Byte, Class, Equiv, Fill };

struct TrElem {
    TrElemKind kind = TrElemKind::Byte;
    unsigned char byte = 0; // Byte, Equiv and Fill: theirs
    std::string className; // Class
    uint64_t count = 1;    // Byte: the [c*n] repeat count, 1 without one
};

// A SET parsed into its elements, and what expanding it gives.
struct TrSpec {
    std::vector<TrElem> elems;

    std::string bytes;                   // the expansion, in order
    std::vector<size_t> caseClassStarts; // where [:upper:]/[:lower:] start
    bool hasClass = false;               // any class at all
    bool hasEquiv = false;
    bool lastIsClass = false;
    size_t fillCount = 0;
};

// Parses one SET. Warnings go to stderr as GNU's do; an error returns false
// after reporting itself.
class TrSetParser {
public:
    TrSetParser(BuiltinContext& context, const std::string& text)
        : m_context(context)
        , m_text(text) {
    }

    bool Parse(TrSpec& spec) {
        const size_t size = m_text.size();
        size_t i = 0;
        while (i < size) {
            if (m_text[i] == '[') {
                if (!ParseBracket(i, spec)) {
                    return false;
                }
                continue;
            }
            const size_t elemStart = i;
            const unsigned char c = NextChar(i);
            // A '-' between two more bytes makes a range; at the start, at
            // the end, or after anything but a plain byte it is literal.
            if (i < size && m_text[i] == '-' && i + 1 < size) {
                ++i;
                const unsigned char c2 = NextChar(i);
                if (c2 < c) {
                    m_context.Error("range-endpoints of "
                        + GnuQuote(m_text.substr(elemStart, i - elemStart))
                        + " are in reverse collating sequence order");
                    return false;
                }
                for (int b = c; b <= c2; ++b) {
                    spec.elems.push_back(TrElem{TrElemKind::Byte,
                        static_cast<unsigned char>(b), "", 1});
                }
                continue;
            }
            spec.elems.push_back(TrElem{TrElemKind::Byte, c, "", 1});
        }
        return true;
    }

private:
    static bool IsOctal(char c) { return c >= '0' && c <= '7'; }

    // The next byte of a SET at |i|: a '\' escape resolved (octal, the
    // letters, or the byte itself), an unescaped '\' at the end kept as a '\'
    // with GNU's warning. Advances |i| past what it took.
    unsigned char NextChar(size_t& i) {
        if (m_text[i] != '\\') {
            return static_cast<unsigned char>(m_text[i++]);
        }
        const size_t j = i + 1;
        if (j >= m_text.size()) {
            m_context.ErrorText(
                "tr: warning: an unescaped backslash at end of string is not portable\n");
            return static_cast<unsigned char>(m_text[i++]);
        }
        const unsigned char escaped = static_cast<unsigned char>(m_text[j]);
        switch (escaped) {
            case 'a': i = j + 1; return '\a';
            case 'b': i = j + 1; return '\b';
            case 'f': i = j + 1; return '\f';
            case 'n': i = j + 1; return '\n';
            case 'r': i = j + 1; return '\r';
            case 't': i = j + 1; return '\t';
            case 'v': i = j + 1; return '\v';
            default: break;
        }
        if (IsOctal(static_cast<char>(escaped))) {
            int value = escaped - '0';
            size_t k = j + 1;
            if (k < m_text.size() && IsOctal(m_text[k])) {
                value = value * 8 + (m_text[k] - '0');
                ++k;
                if (k < m_text.size() && IsOctal(m_text[k])) {
                    if (value * 8 + (m_text[k] - '0') > 0377) {
                        // Three digits past a byte: GNU keeps the first two
                        // and leaves the third an ordinary byte.
                        char first[8];
                        std::snprintf(first, sizeof(first), "%03o", value);
                        m_context.ErrorText("tr: warning: the ambiguous octal escape \\"
                            + m_text.substr(j, 2) + m_text[k]
                            + " is being\n\tinterpreted as the 2-byte sequence \\"
                            + first + ", " + m_text[k] + "\n");
                    } else {
                        value = value * 8 + (m_text[k] - '0');
                        ++k;
                    }
                }
            }
            i = k;
            return static_cast<unsigned char>(value);
        }
        i = j + 1;
        return escaped;
    }

    // A '[' at |i|: [:class:], [=c=], [c*n] or [c*] -- anything else, or one
    // left unterminated, is a literal '['. Advances |i| past what it took;
    // returns false on an error it has reported.
    bool ParseBracket(size_t& i, TrSpec& spec) {
        const size_t size = m_text.size();
        auto literal = [&]() {
            spec.elems.push_back(TrElem{TrElemKind::Byte, '[', "", 1});
            ++i;
        };
        const size_t after = i + 1;
        if (after >= size) {
            literal();
            return true;
        }
        if (m_text[after] == ':') {
            const size_t close = m_text.find(":]", after + 1);
            if (close != std::string::npos) {
                const std::string name = m_text.substr(after + 1, close - after - 1);
                std::string members;
                if (!TrClassMembers(name, members)) {
                    m_context.Error("invalid character class " + GnuQuote(name));
                    return false;
                }
                spec.elems.push_back(TrElem{TrElemKind::Class, 0, name, 1});
                i = close + 2;
                return true;
            }
            literal();
            return true;
        }
        if (m_text[after] == '=') {
            const size_t close = m_text.find("=]", after + 1);
            if (close != std::string::npos) {
                const std::string content = m_text.substr(after + 1, close - after - 1);
                if (content.size() != 1) {
                    // GNU prints the content unquoted here.
                    m_context.ErrorText(
                        content + ": equivalence class operand must be a single character\n");
                    return false;
                }
                spec.elems.push_back(
                    TrElem{TrElemKind::Equiv, static_cast<unsigned char>(content[0]), "", 1});
                i = close + 2;
                return true;
            }
            literal();
            return true;
        }
        // [c*n] or [c*]: one byte, a '*', then a count or ']' right away.
        // Without the '*', the '[' is literal and the byte is parsed again.
        size_t elem = after;
        const unsigned char c = NextChar(elem);
        if (elem >= size || m_text[elem] != '*') {
            literal();
            return true;
        }
        ++elem;
        const size_t countStart = elem;
        std::string digits;
        while (elem < size) {
            const char digit = m_text[elem];
            if (digit < '0' || digit > '9') {
                break;
            }
            if (!digits.empty() && digits[0] == '0' && !IsOctal(digit)) {
                break;
            }
            digits += digit;
            ++elem;
        }
        auto invalid = [&](const std::string& shown) {
            m_context.Error("invalid repeat count " + GnuQuote(shown)
                + " in [c*n] construct");
        };
        if (elem < size && m_text[elem] == ']') {
            if (digits.empty()) {
                // [c*]: string2's fill; string1 rejects it when expanding.
                spec.elems.push_back(TrElem{TrElemKind::Fill, c, "", 0});
            } else {
                uint64_t count = 0;
                if (!ParseRepeatCount(digits, count)) {
                    invalid(digits);
                    return false;
                }
                // A count of 0 is the fill, as GNU takes it.
                spec.elems.push_back(count == 0
                    ? TrElem{TrElemKind::Fill, c, "", 0}
                    : TrElem{TrElemKind::Byte, c, "", count});
            }
            i = elem + 1;
            return true;
        }
        size_t shownEnd = elem;
        while (shownEnd < size && m_text[shownEnd] != ']') {
            ++shownEnd;
        }
        invalid(m_text.substr(countStart, shownEnd - countStart));
        return false;
    }

    static bool ParseRepeatCount(const std::string& digits, uint64_t& out) {
        const bool octal = digits[0] == '0';
        const uint64_t base = octal ? 8 : 10;
        uint64_t value = 0;
        for (const char digit : digits) {
            const uint64_t d = static_cast<uint64_t>(digit - '0');
            if (d >= base) {
                return false;
            }
            if (value > (UINT64_MAX - d) / base) {
                return false;
            }
            value = value * base + d;
        }
        out = value;
        return true;
    }

    BuiltinContext& m_context;
    const std::string& m_text;
};

// Expands a parsed SET to its bytes. |isString1| rejects the [c*] fill;
// |set1Length| is what string2's fill pads string2 up to -- the length of
// string1, or of its complement when -c complements a SET without a class.
bool TrExpand(BuiltinContext& context, const std::vector<TrElem>& elems, bool isString1,
              size_t set1Length, TrSpec& spec) {
    size_t others = 0;
    for (const auto& elem : elems) {
        switch (elem.kind) {
            case TrElemKind::Byte: others += elem.count; break;
            case TrElemKind::Class: {
                std::string members;
                TrClassMembers(elem.className, members);
                others += members.size();
                break;
            }
            case TrElemKind::Equiv: others += 1; break;
            case TrElemKind::Fill: break;
        }
    }
    const size_t fillLength = set1Length > others ? set1Length - others : 0;
    for (const auto& elem : elems) {
        switch (elem.kind) {
            case TrElemKind::Byte:
                spec.bytes.append(elem.count, static_cast<char>(elem.byte));
                break;
            case TrElemKind::Class: {
                std::string members;
                TrClassMembers(elem.className, members);
                if (TrClassIsCase(elem.className)) {
                    spec.caseClassStarts.push_back(spec.bytes.size());
                }
                spec.hasClass = true;
                spec.bytes += members;
                break;
            }
            case TrElemKind::Equiv:
                spec.hasEquiv = true;
                spec.bytes += static_cast<char>(elem.byte);
                break;
            case TrElemKind::Fill:
                if (isString1) {
                    context.Error("the [c*] repeat construct may not appear in string1");
                    return false;
                }
                if (++spec.fillCount > 1) {
                    context.Error("only one [c*] repeat construct may appear in string2");
                    return false;
                }
                spec.bytes.append(fillLength, static_cast<char>(elem.byte));
                break;
        }
    }
    spec.lastIsClass = !elems.empty() && elems.back().kind == TrElemKind::Class;
    return true;
}

class TrCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "tr"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'c', "complement", kTrComplement, BuiltinArgument::None, "",
                "use the complement of SET1"},
            {'C', "", kTrComplement, BuiltinArgument::None, "",
                "same as -c, in the C locale"},
            {'d', "delete", kTrDelete, BuiltinArgument::None, "",
                "delete characters in SET1, do not translate"},
            {'s', "squeeze-repeats", kTrSqueeze, BuiltinArgument::None, "",
                "replace each run of a repeated character that is in the last SET\n"
                "with a single occurrence of that character"},
            {'t', "truncate-set1", kTrTruncate, BuiltinArgument::None, "",
                "first truncate SET1 to the length of SET2"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "translate or delete characters",
            {"tr [OPTION]... SET1 [SET2]"},
            "SETs work on bytes, as GNU tr does with LC_ALL=C. Interpreted sequences:\n"
            "\\\\ backslash, \\a \\b \\f \\n \\r \\t \\v their controls, \\NNN octal (1-3 digits),\n"
            "CHAR1-CHAR2 a range, [:class:] alnum alpha blank cntrl digit graph lower\n"
            "print punct space upper xdigit, [=CHAR=] an equivalence class, [CHAR*n]\n"
            "CHAR repeated, and [CHAR*] in SET2 only: CHAR repeated until SET2 is as\n"
            "long as SET1.",
        };
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }

        bool complement = false;
        bool del = false;
        bool squeeze = false;
        bool truncate = false;
        for (const auto& option : parsed->options) {
            switch (option.id) {
                case kTrComplement: complement = true; break;
                case kTrDelete: del = true; break;
                case kTrSqueeze: squeeze = true; break;
                case kTrTruncate: truncate = true; break;
                default: break;
            }
        }

        // How many operands the mode takes, with GNU's messages.
        const std::vector<std::string>& ops = parsed->operands;
        if (ops.empty()) {
            context.Error("missing operand");
            context.TryHelp();
            return 1;
        }
        const size_t maxOperands = del && !squeeze ? 1 : 2;
        if (ops.size() > maxOperands) {
            context.Error("extra operand " + GnuQuote(ops[maxOperands]));
            if (del && !squeeze) {
                context.ErrorText(
                    "Only one string may be given when deleting without squeezing repeats.\n");
            }
            context.TryHelp();
            return 1;
        }
        const bool twoStrings = ops.size() == 2;
        // Translating needs both strings; -d or -s alone works on one, but
        // -d and -s together need one to delete and one to squeeze.
        if (!del && !squeeze && !twoStrings) {
            context.Error("missing operand after " + GnuQuote(ops[0]));
            context.ErrorText("Two strings must be given when translating.\n");
            context.TryHelp();
            return 1;
        }
        if (del && squeeze && !twoStrings) {
            context.Error("missing operand after " + GnuQuote(ops[0]));
            context.ErrorText("Two strings must be given when both deleting and squeezing repeats.\n");
            context.TryHelp();
            return 1;
        }
        const bool translating = !del && twoStrings;

        // Parse and expand SET1 first, as GNU does.
        TrSpec spec1;
        TrSetParser parser1(context, ops[0]);
        if (!parser1.Parse(spec1)) {
            return 1;
        }
        if (!TrExpand(context, spec1.elems, /*isString1=*/true, 0, spec1)) {
            return 1;
        }
        bool inSet1[256] = {};
        for (const char byte : spec1.bytes) {
            inSet1[static_cast<unsigned char>(byte)] = true;
        }
        // -c over a SET without a class replaces it by every byte not in it,
        // ascending. A SET with a class keeps its bytes as the domain
        // instead: the class bytes stay what they are, everything else maps
        // through SET2's one byte.
        const bool complementedClass = complement && spec1.hasClass;
        std::string set1 = spec1.bytes;
        if (complement && !complementedClass) {
            set1.clear();
            for (int b = 0; b < 256; ++b) {
                if (!inSet1[b]) {
                    set1 += static_cast<char>(b);
                }
            }
        }
        const size_t set1Length = set1.size();

        TrSpec spec2;
        if (twoStrings) {
            TrSetParser parser2(context, ops[1]);
            if (!parser2.Parse(spec2)) {
                return 1;
            }
            if (!TrExpand(context, spec2.elems, /*isString1=*/false, set1Length, spec2)) {
                return 1;
            }
        }

        if (translating) {
            // SET2 may hold only the case classes, and no [=c=].
            for (const auto& elem : spec2.elems) {
                if (elem.kind == TrElemKind::Class && !TrClassIsCase(elem.className)) {
                    context.ErrorText(
                        "tr: when translating, the only character classes that may appear in\n"
                        "string2 are 'upper' and 'lower'\n");
                    return 1;
                }
                if (elem.kind == TrElemKind::Equiv) {
                    context.ErrorText(
                        "tr: [=c=] expressions may not appear in string2 when translating\n");
                    return 1;
                }
            }
            // Each case class of SET2 must start where one of SET1 does.
            if (!complement) {
                for (const size_t start : spec2.caseClassStarts) {
                    if (std::find(spec1.caseClassStarts.begin(), spec1.caseClassStarts.end(),
                                  start) == spec1.caseClassStarts.end()) {
                        context.ErrorText("tr: misaligned [:upper:] and/or [:lower:] construct\n");
                        return 1;
                    }
                }
            }
            if (!truncate && set1Length > spec2.bytes.size() && spec2.lastIsClass) {
                context.ErrorText(
                    "tr: when translating with string1 longer than string2,\n"
                    "the latter string must not end with a character class\n");
                return 1;
            }
            // With -c and a class in SET1, SET2 maps the whole domain to one
            // byte: a single byte, or one [c*].
            if (complementedClass) {
                const bool singleByte = spec2.elems.size() == 1
                    && spec2.elems[0].kind == TrElemKind::Byte
                    && spec2.elems[0].count == 1;
                const bool singleFill =
                    spec2.elems.size() == 1 && spec2.elems[0].kind == TrElemKind::Fill;
                if (!singleByte && !singleFill) {
                    context.ErrorText(
                        "tr: when translating with complemented character classes,\n"
                        "string2 must map all characters in the domain to one\n");
                    return 1;
                }
            }
            if (!truncate && spec2.bytes.empty() && set1Length > 0) {
                context.ErrorText("tr: when not truncating set1, string2 must be non-empty\n");
                return 1;
            }
        }

        // What each input byte becomes.
        unsigned char map[256];
        for (int b = 0; b < 256; ++b) {
            map[b] = static_cast<unsigned char>(b);
        }
        if (translating) {
            if (complementedClass) {
                const unsigned char target =
                    spec2.bytes.empty() ? 0 : static_cast<unsigned char>(spec2.bytes[0]);
                for (int b = 0; b < 256; ++b) {
                    map[b] = target;
                }
                for (int b = 0; b < 256; ++b) {
                    if (inSet1[b]) {
                        map[b] = static_cast<unsigned char>(b);
                    }
                }
            } else {
                std::string left = set1;
                std::string right = spec2.bytes;
                if (truncate && right.size() < left.size()) {
                    left.resize(right.size());
                } else if (right.size() < left.size() && !right.empty()) {
                    right.resize(left.size(), right.back());
                }
                const size_t pairs = std::min(left.size(), right.size());
                for (size_t i = 0; i < pairs; ++i) {
                    map[static_cast<unsigned char>(left[i])] =
                        static_cast<unsigned char>(right[i]);
                }
            }
        }

        // What gets deleted, and what gets squeezed.
        bool deleteSet[256] = {};
        bool squeezeSet[256] = {};
        if (del) {
            for (int b = 0; b < 256; ++b) {
                deleteSet[b] = complement ? !inSet1[b] : inSet1[b];
            }
        }
        if (squeeze) {
            const TrSpec& squeezeSpec = twoStrings ? spec2 : spec1;
            for (const char byte : squeezeSpec.bytes) {
                squeezeSet[static_cast<unsigned char>(byte)] = true;
            }
            if (!twoStrings && complement) {
                for (int b = 0; b < 256; ++b) {
                    squeezeSet[b] = !squeezeSet[b];
                }
            }
        }

        const auto input = context.IO().GetDescriptor(IFileIO::kStdIn);
        if (!input) {
            return 0;
        }
        char buffer[64 * 1024];
        int lastByte = -1; // the last byte kept, for squeezing runs
        while (true) {
            if (context.StopRequested()) {
                return 0;
            }
            const ssize_t n = input->Read(buffer, sizeof(buffer));
            if (n == 0) {
                return 0;
            }
            if (n == kIOInterrupted) {
                return 0; // quiet: the process reports its own stop code
            }
            if (n < 0) {
                context.Error("read error: Input/output error");
                return 1;
            }
            std::string out;
            out.reserve(static_cast<size_t>(n));
            for (ssize_t i = 0; i < n; ++i) {
                unsigned char byte = static_cast<unsigned char>(buffer[i]);
                if (del && deleteSet[byte]) {
                    continue;
                }
                if (translating) {
                    byte = map[byte];
                }
                if (squeeze && lastByte == byte && squeezeSet[byte]) {
                    continue;
                }
                lastByte = byte;
                out += static_cast<char>(byte);
            }
            context.Out(out);
        }
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateTrCommand() {
    return std::make_shared<TrCommand>();
}

} // namespace Haisos