#include "BuiltinFnmatch.h"
#include <string>
#include <vector>

namespace Haisos {
namespace {

// ASCII case folding, as the C locale's.
char FoldByte(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return static_cast<char>(u >= 'A' && u <= 'Z' ? u - 'A' + 'a' : u);
}

bool BytesEqualFold(char a, char b, bool fold) {
    return fold ? FoldByte(a) == FoldByte(b) : a == b;
}

// The character classes of the C locale, [:alnum:] through [:xdigit:]
// (glibc's C locale has no [:ascii:]).
enum BracketClass {
    kClassUnknown = 0,
    kClassAlnum, kClassAlpha, kClassBlank, kClassCntrl, kClassDigit,
    kClassGraph, kClassLower, kClassPrint, kClassPunct, kClassSpace,
    kClassUpper, kClassXdigit,
};

BracketClass ClassId(std::string_view name) {
    const struct { const char* name; BracketClass id; } kClasses[] = {
        {"alnum", kClassAlnum}, {"alpha", kClassAlpha}, {"blank", kClassBlank},
        {"cntrl", kClassCntrl}, {"digit", kClassDigit}, {"graph", kClassGraph},
        {"lower", kClassLower}, {"print", kClassPrint}, {"punct", kClassPunct},
        {"space", kClassSpace}, {"upper", kClassUpper}, {"xdigit", kClassXdigit},
    };
    for (const auto& c : kClasses) {
        if (name == c.name) {
            return c.id;
        }
    }
    return kClassUnknown;
}

bool ClassMatches(BracketClass id, unsigned char c) {
    switch (id) {
        case kClassAlnum: return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        case kClassAlpha: return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        case kClassBlank: return c == ' ' || c == '\t';
        case kClassCntrl: return c < 0x20 || c == 0x7F;
        case kClassDigit: return c >= '0' && c <= '9';
        case kClassGraph: return c > 0x20 && c < 0x7F;
        case kClassLower: return c >= 'a' && c <= 'z';
        case kClassPrint: return c >= 0x20 && c < 0x7F;
        case kClassPunct: return (c > 0x20 && c < 0x7F) && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'));
        case kClassSpace: return c == ' ' || (c >= '\t' && c <= '\r');
        case kClassUpper: return c >= 'A' && c <= 'Z';
        case kClassXdigit: return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        default: return false;
    }
}

// A bracket expression parsed out of a pattern, as glibc reads one: members
// (a single byte is a range of one), [:class:] items, negation, and the index
// past its closing ']' (npos when it never closes: then the '[' is a literal).
struct BracketExpr {
    struct Range {
        char lo;
        char hi;  // hi < lo: a reversed range, which matches nothing
    };
    std::vector<Range> ranges;
    std::vector<BracketClass> classes;
    size_t end = std::string_view::npos;
    bool negated = false;
    // An unknown [:class:] name: the bracket matches nothing at all, negated
    // or not (glibc's, verified through the C library).
    bool impossible = false;

    bool Matches(unsigned char ch, bool fold) const {
        if (impossible) {
            return false;
        }
        const char c = static_cast<char>(ch);
        bool in = false;
        for (const BracketClass id : classes) {
            in = in || ClassMatches(id, ch);
        }
        for (const Range& r : ranges) {
            char lo = r.lo;
            char hi = r.hi;
            char f = c;
            if (fold) {
                lo = FoldByte(lo);
                hi = FoldByte(hi);
                f = FoldByte(f);
            }
            if (lo <= hi && f >= lo && f <= hi) {
                in = true;
            }
        }
        return negated ? !in : in;
    }
};

// Parses the bracket opening at |open| ('['). A '\' escapes the next byte
// inside unless |noEscape|; ']' right after '[' (or '[!'/'[^') is a member;
// '-' first or last of the content is one; 'a-z' is a range; [:class:] is a
// character class; content running past the pattern's end means the '[' is a
// literal byte (|end| stays npos).
BracketExpr ParseBracket(std::string_view pattern, size_t open, bool noEscape) {
    BracketExpr bracket;
    size_t i = open + 1;
    if (i < pattern.size() && (pattern[i] == '!' || pattern[i] == '^')) {
        bracket.negated = true;
        ++i;
    }
    bool first = true;
    while (true) {
        if (i >= pattern.size()) {
            return bracket;  // never closed: a literal '['
        }
        if (pattern[i] == ']' && !first) {
            bracket.end = i + 1;
            return bracket;
        }
        first = false;
        // One member: a character class, a single byte (possibly escaped) or
        // the low end of a range.
        if (pattern[i] == '[' && i + 1 < pattern.size() && pattern[i + 1] == ':') {
            size_t j = i + 2;
            while (j < pattern.size() && pattern[j] != ':') {
                ++j;
            }
            if (j < pattern.size() && j + 1 < pattern.size() && pattern[j + 1] == ']') {
                const BracketClass id = ClassId(pattern.substr(i + 2, j - (i + 2)));
                if (id == kClassUnknown) {
                    bracket.impossible = true;
                } else {
                    bracket.classes.push_back(id);
                }
                i = j + 2;
                continue;  // a class is never a range's endpoint
            }
        }
        char lo = 0;
        if (!noEscape && pattern[i] == '\\' && i + 1 < pattern.size()) {
            lo = pattern[i + 1];
            i += 2;
        } else {
            lo = pattern[i];
            ++i;
        }
        if (i + 1 < pattern.size() && pattern[i] == '-' && pattern[i + 1] != ']') {
            ++i;  // past '-'
            char hi = 0;
            if (!noEscape && pattern[i] == '\\' && i + 1 < pattern.size()) {
                hi = pattern[i + 1];
                i += 2;
            } else {
                hi = pattern[i];
                ++i;
            }
            bracket.ranges.push_back({lo, hi});
        } else {
            bracket.ranges.push_back({lo, lo});
        }
    }
}

// Whether the pattern item at |p| is a literal '.' (a bare '.', or an escaped
// one): what a leading '.' must be matched by under kFnmPeriod.
bool IsLiteralDot(std::string_view pattern, size_t p, bool noEscape) {
    if (p >= pattern.size()) {
        return false;
    }
    if (!noEscape && pattern[p] == '\\') {
        return p + 1 < pattern.size() && pattern[p + 1] == '.';
    }
    return pattern[p] == '.';
}

// Whether one pattern segment matches one text segment whole: '*', '?' and
// literals, with single-star backtracking (the last '*' absorbs one more
// byte at a time, so a pattern of many '*' stays linear-ish on a long text).
// |period|: a '.' at the segment's start must be matched by a literal '.'.
bool SegmentMatch(std::string_view pattern, std::string_view text, bool noEscape, bool fold, bool period) {
    size_t p = 0;
    size_t t = 0;
    size_t starP = std::string_view::npos;  // where the last '*' is
    size_t starT = 0;                       // the text byte it has absorbed up to
    while (t < text.size()) {
        // A leading '.', or one after a '/' with kFnmPathname, is never
        // matched by '*', '?' or a bracket.
        if (period && t == 0 && text[0] == '.' && !IsLiteralDot(pattern, p, noEscape)) {
            return false;
        }
        if (p < pattern.size()) {
            const char pc = pattern[p];
            if (pc == '*') {
                starP = p;
                starT = t;
                ++p;
                continue;
            }
            bool ok = false;
            size_t next = p + 1;
            if (pc == '?') {
                ok = true;
            } else if (pc == '[') {
                const BracketExpr bracket = ParseBracket(pattern, p, noEscape);
                if (bracket.end != std::string_view::npos) {
                    next = bracket.end;
                    ok = bracket.Matches(static_cast<unsigned char>(text[t]), fold);
                } else {
                    ok = text[t] == '[';  // never closed: a literal '['
                }
            } else if (!noEscape && pc == '\\') {
                // A trailing lone '\' loses: it never matches anything
                // (glibc's rule), and as a literal '\' it does not count
                // either.
                if (p + 1 < pattern.size()) {
                    ok = BytesEqualFold(pattern[p + 1], text[t], fold);
                    next = p + 2;
                }
            } else {
                ok = BytesEqualFold(pc, text[t], fold);
            }
            if (ok) {
                p = next;
                ++t;
                continue;
            }
        }
        // Mismatch: the last '*' absorbs one more byte and matching resumes
        // right after it. A trailing lone '\' never matches anything, so it
        // fails like any other mismatch.
        if (starP == std::string_view::npos) {
            return false;
        }
        ++starT;
        t = starT;
        p = starP + 1;
    }
    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }
    return p == pattern.size();
}

// Splits |text| at every '/': "a/b" -> "a", "b"; "" -> one empty segment.
std::vector<std::string_view> SplitSegments(std::string_view text) {
    std::vector<std::string_view> segments;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '/') {
            segments.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    segments.push_back(text.substr(start));
    return segments;
}

// Splits the pattern at the '/'s that separate segments under kFnmPathname:
// a '/' inside a closed bracket is a member, not a separator, and an escaped
// '\/' is a separator too (glibc unescapes before looking for '/').
std::vector<std::string_view> SplitPattern(std::string_view pattern, bool noEscape) {
    std::vector<std::string_view> segments;
    size_t start = 0;
    size_t i = 0;
    while (i < pattern.size()) {
        const char c = pattern[i];
        if (c == '/') {
            segments.push_back(pattern.substr(start, i - start));
            start = i + 1;
            ++i;
        } else if (c == '\\') {
            if (noEscape) {
                ++i;
            } else if (i + 1 >= pattern.size()) {
                break;  // a trailing lone '\' can never match anyway
            } else if (pattern[i + 1] == '/') {
                segments.push_back(pattern.substr(start, i - start));
                start = i + 2;
                i += 2;
            } else {
                i += 2;
            }
        } else if (c == '[') {
            const BracketExpr bracket = ParseBracket(pattern, i, noEscape);
            if (bracket.end != std::string_view::npos) {
                i = bracket.end;
            } else {
                ++i;  // a literal '[': keep scanning for a separator
            }
        } else {
            ++i;
        }
    }
    segments.push_back(pattern.substr(start));
    return segments;
}

// The whole pattern against the whole text, kFnmPathname included.
bool MatchWhole(std::string_view pattern, std::string_view text, int flags) {
    const bool noEscape = (flags & kFnmNoEscape) != 0;
    const bool fold = (flags & kFnmCaseFold) != 0;
    const bool period = (flags & kFnmPeriod) != 0;
    if (flags & kFnmPathname) {
        // Each pattern segment matches exactly one text segment whole, so
        // '*', '?' and brackets never cross a '/'.
        const std::vector<std::string_view> psegs = SplitPattern(pattern, noEscape);
        const std::vector<std::string_view> tsegs = SplitSegments(text);
        if (psegs.size() != tsegs.size()) {
            return false;
        }
        for (size_t i = 0; i < psegs.size(); ++i) {
            if (!SegmentMatch(psegs[i], tsegs[i], noEscape, fold, period)) {
                return false;
            }
        }
        return true;
    }
    return SegmentMatch(pattern, text, noEscape, fold, period);
}

} // namespace

bool FnMatch(std::string_view pattern, std::string_view text, int flags) {
    if (MatchWhole(pattern, text, flags)) {
        return true;
    }
    // kFnmLeadingDir: the pattern also matches a prefix of the text that a
    // '/' follows.
    if (flags & kFnmLeadingDir) {
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '/' && MatchWhole(pattern, text.substr(0, i), flags)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace Haisos