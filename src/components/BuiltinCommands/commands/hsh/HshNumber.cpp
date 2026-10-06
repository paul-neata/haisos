#include "commands/hsh/HshNumber.h"

#include <cstdint>
#include <limits>

namespace Haisos::Hsh {

namespace {
bool IsBlank(char c) {  // C-locale isspace
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}
} // namespace

std::optional<intmax_t> Atomax10(const std::string& text) {
    size_t pos = text.find_first_not_of(" \t\n\v\f\r");
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    bool negative = false;
    if (text[pos] == '-' || text[pos] == '+') {
        negative = text[pos] == '-';
        ++pos;
    }
    if (pos == text.size() || text[pos] < '0' || text[pos] > '9') {
        return std::nullopt;
    }
    // The magnitude, accumulated in uintmax_t; the limit is INTMAX_MAX, plus
    // one when negative (INTMAX_MIN).
    const uintmax_t limit =
        static_cast<uintmax_t>(std::numeric_limits<intmax_t>::max()) + (negative ? 1 : 0);
    uintmax_t value = 0;
    bool overflow = false;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
        const uintmax_t digit = static_cast<uintmax_t>(text[pos] - '0');
        if (value > (limit - digit) / 10) {
            overflow = true;
        } else if (!overflow) {
            value = value * 10 + digit;
        }
        ++pos;
    }
    if (overflow) {
        return std::nullopt;
    }
    while (pos < text.size() && IsBlank(text[pos])) {
        ++pos;
    }
    if (pos != text.size()) {
        return std::nullopt;
    }
    if (negative) {
        if (value > static_cast<uintmax_t>(std::numeric_limits<intmax_t>::max())) {
            return std::numeric_limits<intmax_t>::min();
        }
        return -static_cast<intmax_t>(value);
    }
    return static_cast<intmax_t>(value);
}

} // namespace Haisos::Hsh
