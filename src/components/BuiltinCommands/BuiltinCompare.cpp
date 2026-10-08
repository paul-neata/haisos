#include "BuiltinCompare.h"
#include <cmath>
#include <cstdlib>
#include <string>

namespace Haisos {
namespace {

bool IsDigit(char c) {
    return c >= '0' && c <= '9';
}

bool IsAsciiAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool IsAsciiAlnum(char c) {
    return IsDigit(c) || IsAsciiAlpha(c);
}

// A number as sort -n reads it: sign and the significant digits of the
// integer and fraction parts (leading zeros of the integer stripped, trailing
// zeros of the fraction stripped), so that equal values compare equal as
// strings of digits.
struct ParsedNumber {
    bool negative = false;
    bool zero = true;
    std::string_view integer;
    std::string_view fraction;
};

ParsedNumber ParseNumber(std::string_view text) {
    ParsedNumber number;
    size_t i = 0;
    if (i < text.size() && text[i] == '-') {
        number.negative = true;
        ++i;
    }
    size_t integerStart = i;
    while (i < text.size() && IsDigit(text[i])) {
        ++i;
    }
    const size_t integerEnd = i;
    size_t fractionStart = i;
    size_t fractionEnd = i;
    if (i < text.size() && text[i] == '.') {
        ++i;
        fractionStart = i;
        while (i < text.size() && IsDigit(text[i])) {
            ++i;
        }
        fractionEnd = i;
    }
    while (integerStart < integerEnd && text[integerStart] == '0') {
        ++integerStart;
    }
    while (fractionEnd > fractionStart && text[fractionEnd - 1] == '0') {
        --fractionEnd;
    }
    number.integer = text.substr(integerStart, integerEnd - integerStart);
    number.fraction = text.substr(fractionStart, fractionEnd - fractionStart);
    number.zero = number.integer.empty() && number.fraction.empty();
    return number;
}

// The magnitude of two positive (or both negative) numbers: by integer part
// (more significant digits = larger, then digit by digit), then by fraction
// digit by digit -- trailing zeros already stripped, so a longer remaining
// fraction is the larger one.
int CompareMagnitudes(const ParsedNumber& a, const ParsedNumber& b) {
    if (a.integer.size() != b.integer.size()) {
        return a.integer.size() < b.integer.size() ? -1 : 1;
    }
    if (!a.integer.empty()) {
        const int diff = a.integer.compare(b.integer);
        if (diff != 0) {
            return diff < 0 ? -1 : 1;
        }
    }
    const size_t common = a.fraction.size() < b.fraction.size() ? a.fraction.size() : b.fraction.size();
    if (common != 0) {
        const int diff = a.fraction.compare(0, common, b.fraction.data(), common);
        if (diff != 0) {
            return diff < 0 ? -1 : 1;
        }
    }
    if (a.fraction.size() != b.fraction.size()) {
        return a.fraction.size() < b.fraction.size() ? -1 : 1;
    }
    return 0;
}

} // namespace

int CompareNumeric(std::string_view a, std::string_view b) {
    const ParsedNumber na = ParseNumber(a);
    const ParsedNumber nb = ParseNumber(b);
    // Sign first: a negative non-zero < zero < positive.
    const int signA = na.zero ? 0 : (na.negative ? -1 : 1);
    const int signB = nb.zero ? 0 : (nb.negative ? -1 : 1);
    if (signA != signB) {
        return signA < signB ? -1 : 1;
    }
    if (signA == 0) {
        return 0;
    }
    const int diff = CompareMagnitudes(na, nb);
    return signA < 0 ? -diff : diff;
}

// --- General numeric (-g) ---

// A text as -g classifies it: not a number (strtold converted nothing), a
// NaN, or a number by value.
struct GeneralNumber {
    // 0: not a number, 1: NaN, 2: a number.
    int kind = 0;
    long double value = 0;
};

GeneralNumber ParseGeneralNumber(std::string_view text) {
    GeneralNumber number;
    // A NUL-terminated copy: strtold needs one, and a key may hold NULs.
    const std::string copy(text);
    char* end = nullptr;
    const long double value = std::strtold(copy.c_str(), &end);
    if (end == copy.c_str()) {
        return number;  // nothing converted: not a number
    }
    if (std::isnan(value)) {
        number.kind = 1;
        return number;
    }
    number.kind = 2;
    number.value = value;
    return number;
}

int CompareGeneralNumeric(std::string_view a, std::string_view b) {
    const GeneralNumber na = ParseGeneralNumber(a);
    const GeneralNumber nb = ParseGeneralNumber(b);
    // not-a-number < NaN < numbers.
    if (na.kind != nb.kind) {
        return na.kind < nb.kind ? -1 : 1;
    }
    if (na.kind != 2) {
        return 0;  // equal as not-a-numbers, or as NaNs
    }
    if (na.value < nb.value) {
        return -1;
    }
    return na.value > nb.value ? 1 : 0;
}

// --- Human numeric (-h) ---

// The unit order of a text: after an optional '-', its number's digits, then
// the byte right after them as a unit -- K or k 1, M 2, ..., Q 10 -- negated
// by the '-'. A number with no non-zero digit (or none at all) has order 0.
int HumanUnitOrder(std::string_view text) {
    size_t i = 0;
    bool negative = false;
    if (i < text.size() && text[i] == '-') {
        negative = true;
        ++i;
    }
    bool anyNonZero = false;
    while (i < text.size() && IsDigit(text[i])) {
        anyNonZero = anyNonZero || text[i] != '0';
        ++i;
    }
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && IsDigit(text[i])) {
            anyNonZero = anyNonZero || text[i] != '0';
            ++i;
        }
    }
    if (!anyNonZero) {
        return 0;
    }
    int order = 0;
    if (i < text.size()) {
        switch (text[i]) {
            case 'K': case 'k': order = 1; break;
            case 'M': order = 2; break;
            case 'G': order = 3; break;
            case 'T': order = 4; break;
            case 'P': order = 5; break;
            case 'E': order = 6; break;
            case 'Z': order = 7; break;
            case 'Y': order = 8; break;
            case 'R': order = 9; break;
            case 'Q': order = 10; break;
            default: order = 0; break;
        }
    }
    return negative ? -order : order;
}

int CompareHumanNumeric(std::string_view a, std::string_view b) {
    const int unitA = HumanUnitOrder(a);
    const int unitB = HumanUnitOrder(b);
    if (unitA != unitB) {
        return unitA < unitB ? -1 : 1;
    }
    return CompareNumeric(a, b);
}

// --- Month (-M) ---

// A text's month: after its leading blanks, its first three bytes compared
// case-insensitively with the abbreviations, 1..12; anything else 0.
int MonthValue(std::string_view text) {
    static const char kMonths[12][4] = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
    };
    size_t i = 0;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
        ++i;
    }
    if (text.size() - i < 3) {
        return 0;  // not even three bytes to name a month with
    }
    for (int month = 0; month < 12; ++month) {
        bool same = true;
        for (int byte = 0; byte < 3; ++byte) {
            char c = text[i + byte];
            if (c >= 'a' && c <= 'z') {
                c = static_cast<char>(c - 'a' + 'A');
            }
            same = same && c == kMonths[month][byte];
        }
        if (same) {
            return month + 1;
        }
    }
    return 0;
}

int CompareMonth(std::string_view a, std::string_view b) {
    const int monthA = MonthValue(a);
    const int monthB = MonthValue(b);
    if (monthA != monthB) {
        return monthA < monthB ? -1 : 1;
    }
    return 0;
}

// --- Version (-V, gnulib filevercmp) ---

// The "order" of a byte in a non-digit run: the end of a text sorts before
// everything but '~'; a digit (compared as its run), a letter as itself,
// '~' first of all, any other byte after every letter.
int VersionByteOrder(unsigned char c) {
    if (IsDigit(c)) {
        return 0;
    }
    if (IsAsciiAlpha(c)) {
        return c;
    }
    if (c == '~') {
        return -2;
    }
    return c + 256;
}

// dpkg's verrevcmp: non-digit runs by VersionByteOrder (the end of a text
// as -1), then digit runs with leading zeros skipped -- the longer run
// larger, else the first differing digit.
int VerRevCmp(std::string_view a, std::string_view b) {
    size_t i = 0;
    size_t j = 0;
    while (i < a.size() || j < b.size()) {
        while ((i < a.size() && !IsDigit(a[i])) || (j < b.size() && !IsDigit(b[j]))) {
            const int orderA = i < a.size() ? VersionByteOrder(a[i]) : -1;
            const int orderB = j < b.size() ? VersionByteOrder(b[j]) : -1;
            if (orderA != orderB) {
                return orderA < orderB ? -1 : 1;
            }
            ++i;
            ++j;
        }
        while (i < a.size() && a[i] == '0') {
            ++i;
        }
        while (j < b.size() && b[j] == '0') {
            ++j;
        }
        int firstDiff = 0;
        while (i < a.size() && j < b.size() && IsDigit(a[i]) && IsDigit(b[j])) {
            if (firstDiff == 0) {
                firstDiff = static_cast<unsigned char>(a[i]) - static_cast<unsigned char>(b[j]);
            }
            ++i;
            ++j;
        }
        if (i < a.size() && IsDigit(a[i])) {
            return 1;  // the longer digit run is the larger number
        }
        if (j < b.size() && IsDigit(b[j])) {
            return -1;
        }
        if (firstDiff != 0) {
            return firstDiff < 0 ? -1 : 1;
        }
    }
    return 0;
}

// The length of the prefix left after cutting the version-like suffix: the
// longest tail of groups '.' + letter-or-'~' + alphanumerics-and-'~' -- as
// gnulib's file_prefixlen scans it. A group chain that does not reach the
// end of the text is no suffix at all.
size_t VersionPrefixLength(std::string_view s) {
    const size_t len = s.size();
    size_t i = 0;
    size_t prefixlen = 0;
    while (i < len) {
        ++i;
        prefixlen = i;
        while (i + 1 < len && s[i] == '.' && (IsAsciiAlpha(s[i + 1]) || s[i + 1] == '~')) {
            i += 2;
            while (i < len && (IsAsciiAlnum(s[i]) || s[i] == '~')) {
                ++i;
            }
        }
    }
    return prefixlen;
}

int CompareVersion(std::string_view a, std::string_view b) {
    if (a == b) {
        return 0;
    }
    // An empty text sorts before any other.
    if (a.empty()) {
        return -1;
    }
    if (b.empty()) {
        return 1;
    }
    // Leading dots: a text starting with one sorts first -- and among them,
    // '.' itself, then '..', then the rest.
    const bool dotA = a[0] == '.';
    const bool dotB = b[0] == '.';
    if (dotA && !dotB) {
        return -1;
    }
    if (!dotA && dotB) {
        return 1;
    }
    if (dotA && dotB) {
        if (a == ".") {
            return -1;
        }
        if (b == ".") {
            return 1;
        }
        if (a == "..") {
            return -1;
        }
        if (b == "..") {
            return 1;
        }
    }
    // The version-like suffixes cut, the prefixes compared; equal prefixes
    // with a suffix somewhere: the whole texts decide.
    const size_t prefixA = VersionPrefixLength(a);
    const size_t prefixB = VersionPrefixLength(b);
    const bool suffixA = prefixA < a.size();
    const bool suffixB = prefixB < b.size();
    const int diff = VerRevCmp(a.substr(0, prefixA), b.substr(0, prefixB));
    if (diff != 0 || (!suffixA && !suffixB)) {
        return diff;
    }
    return VerRevCmp(a, b);
}

} // namespace Haisos