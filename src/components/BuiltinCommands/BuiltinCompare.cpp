#include "BuiltinCompare.h"

namespace Haisos {
namespace {

bool IsDigit(char c) {
    return c >= '0' && c <= '9';
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

} // namespace Haisos