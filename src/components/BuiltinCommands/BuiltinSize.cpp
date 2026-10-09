#include "BuiltinSize.h"
#include <cmath>
#include <cstdio>

namespace Haisos {
namespace {

// Whether a byte is a blank xstrtoumax skips over.
bool IsBlank(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

// 10^|power|, or 0 when it does not fit a uint64_t.
uint64_t PowerOf10(int power) {
    uint64_t value = 1;
    for (int i = 0; i < power; ++i) {
        if (value > (~uint64_t{0}) / 10) {
            return 0;
        }
        value *= 10;
    }
    return value;
}

} // namespace

SizeParse ParseSizeWithSuffix(std::string_view text, std::string_view validSuffixes, uintmax_t& out) {
    const uintmax_t max = ~uintmax_t{0};
    size_t pos = 0;
    while (pos < text.size() && IsBlank(text[pos])) {
        ++pos;
    }
    if (pos < text.size() && (text[pos] == '-' || text[pos] == '+')) {
        // gnulib takes a '+' sign and refuses a '-' one.
        if (text[pos] == '-') {
            out = 0;
            return SizeParse::Invalid;
        }
        ++pos;
    }
    uintmax_t value = 0;
    bool hasDigits = false;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
        hasDigits = true;
        const unsigned digit = static_cast<unsigned>(text[pos] - '0');
        if (value > (max - digit) / 10) {
            out = max;
            return SizeParse::Overflow;
        }
        value = value * 10 + digit;
        ++pos;
    }
    if (!hasDigits) {
        // No digits: the value is 1 when a valid suffix follows, else invalid.
        if (pos >= text.size() || validSuffixes.find(text[pos]) == std::string_view::npos) {
            out = 0;
            return SizeParse::Invalid;
        }
        value = 1;
    }
    if (pos < text.size() && validSuffixes.find(text[pos]) != std::string_view::npos) {
        const char suffix = text[pos++];
        // b c w are fixed multipliers; the letters after them are powers, each
        // case of a letter accepted alone (k and K both 1024, g and G both
        // 1024^3, as gnulib takes them).
        uint64_t multiplier = 0;
        int power = 0;
        switch (suffix) {
            case 'b': multiplier = 512; break;
            case 'c': multiplier = 1; break;
            case 'w': multiplier = 2; break;
            case 'k': case 'K': power = 1; break;
            case 'm': case 'M': power = 2; break;
            case 'g': case 'G': power = 3; break;
            case 't': case 'T': power = 4; break;
            case 'p': case 'P': power = 5; break;
            case 'e': case 'E': power = 6; break;
            case 'z': case 'Z': power = 7; break;
            case 'y': case 'Y': power = 8; break;
            case 'r': case 'R': power = 9; break;
            case 'q': case 'Q': power = 10; break;
            default: break;
        }
        // With '0' among the valid suffixes a power letter may be followed by
        // "iB" (1024-based, the plain power of 1024) or "B"/"D" (1000-based:
        // 10^3 per level).
        bool base1000 = false;
        if (power > 0 && validSuffixes.find('0') != std::string_view::npos) {
            if (pos + 1 < text.size() && text[pos] == 'i' && text[pos + 1] == 'B') {
                pos += 2;
            } else if (pos < text.size() && (text[pos] == 'B' || text[pos] == 'D')) {
                ++pos;
                base1000 = true;
            }
        }
        if (power > 0) {
            multiplier = base1000 ? PowerOf10(3 * power) : (power <= 6 ? (uint64_t{1024} << (10 * (power - 1))) : 0);
        }
        if (multiplier == 0 || value > max / multiplier) {
            out = max;
            return SizeParse::Overflow;
        }
        value *= multiplier;
    }
    if (pos < text.size()) {
        out = value;
        return SizeParse::InvalidSuffix;
    }
    out = value;
    return SizeParse::Ok;
}

std::string FormatHumanSize(uint64_t bytes, bool si) {
    const uint64_t base = si ? 1000 : 1024;
    if (bytes < base) {
        return std::to_string(bytes);
    }
    // GNU's own units: powers of 1024 are written K M G ..., of 1000 k M G ...
    static const char kUnits1024[] = "KMGTPEZY";
    static const char kUnits1000[] = "kMGTPEZY";
    const char* const units = si ? kUnits1000 : kUnits1024;
    const size_t unitCount = sizeof(kUnits1024) - 1;
    double value = static_cast<double>(bytes);
    size_t unit = 0;
    value /= static_cast<double>(base);
    while (true) {
        const double shown = value < 10 ? std::ceil(value * 10) / 10 : std::ceil(value);
        if (shown < static_cast<double>(base) || unit + 1 >= unitCount) {
            char buffer[32];
            if (shown < 10) {
                std::snprintf(buffer, sizeof(buffer), "%.1f%c", shown, units[unit]);
            } else {
                std::snprintf(buffer, sizeof(buffer), "%.0f%c", shown, units[unit]);
            }
            return buffer;
        }
        value /= static_cast<double>(base);
        ++unit;
    }
}

} // namespace Haisos