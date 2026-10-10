#include "commands/awk/AwkValue.h"
#include "BuiltinPrintf.h"
#include <cmath>
#include <cstdlib>
#include <limits>

namespace Haisos::Awk {

// --- Value ---

Value::Value()
    : m_type(Type::Uninitialized)
    , m_number(0.0) {
}

Value Value::FromNumber(double number) {
    Value value;
    value.m_type = Type::Number;
    value.m_number = number;
    return value;
}

Value Value::FromString(std::string text) {
    Value value;
    value.m_type = Type::String;
    value.m_text = std::move(text);
    return value;
}

Value Value::FromInput(std::string text) {
    double number = 0.0;
    if (LooksNumeric(text, number)) {
        Value value;
        value.m_type = Type::StrNum;
        value.m_number = number;
        value.m_text = std::move(text);
        return value;
    }
    return FromString(std::move(text));
}

Value::Type Value::GetType() const {
    return m_type;
}

bool Value::IsNumeric() const {
    // Uninitialized counts: an unset variable compares numerically, as 0.
    return m_type != Type::String;
}

double Value::ToNumber() const {
    switch (m_type) {
        case Type::Number:
        case Type::StrNum:
            return m_number;
        case Type::String:
            return StringToNumber(m_text);
        case Type::Uninitialized:
        default:
            return 0.0;
    }
}

std::string Value::ToString(const std::string& format) const {
    switch (m_type) {
        case Type::Number:
            return AwkNumberToString(m_number, format);
        case Type::String:
        case Type::StrNum:
            return m_text;
        case Type::Uninitialized:
        default:
            return std::string();
    }
}

bool Value::ToBoolean() const {
    switch (m_type) {
        case Type::Number:
        case Type::StrNum:
            return m_number != 0.0;
        case Type::String:
            return !m_text.empty();
        case Type::Uninitialized:
        default:
            return false;
    }
}

// --- Conversions ---

namespace {

// The blanks std::strtod skips before a number: space, tab, newline,
// vertical tab, form feed, carriage return.
bool IsAwkBlank(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

// The number truncated toward zero, clamped to intmax_t's range (a double
// beyond it has no integer to become; NaN is 0, as gawk's conversions give).
intmax_t TruncateToIntmax(double number) {
    if (std::isnan(number)) {
        return 0;
    }
    if (number >= static_cast<double>(std::numeric_limits<intmax_t>::max())) {
        return std::numeric_limits<intmax_t>::max();
    }
    if (number <= static_cast<double>(std::numeric_limits<intmax_t>::min())) {
        return std::numeric_limits<intmax_t>::min();
    }
    return static_cast<intmax_t>(std::trunc(number));
}

} // namespace

double StringToNumber(std::string_view text) {
    // The blanks before the number are strtod's own; nothing parsed is 0.
    const std::string copy(text);
    char* end = nullptr;
    return std::strtod(copy.c_str(), &end);
}

bool LooksNumeric(std::string_view text, double& number) {
    // std::strtod needs a NUL-terminated string and skips the leading blanks
    // itself; it parses the longest prefix that is a number (decimal, hex
    // "0x1A", "inf", "infinity", "nan", any case) and leaves endptr at the
    // start when there is none.
    const std::string copy(text);
    const char* begin = copy.c_str();
    char* end = nullptr;
    number = std::strtod(begin, &end);
    if (end == begin) {
        number = 0.0;
        return false;   // nothing was parsed
    }
    while (end < begin + copy.size() && IsAwkBlank(*end)) {
        ++end;
    }
    return end == begin + copy.size();
}

std::string AwkNumberToString(double number, const std::string& format) {
    if (std::isnan(number)) {
        return std::signbit(number) ? "-nan" : "+nan";
    }
    if (std::isinf(number)) {
        return std::signbit(number) ? "-inf" : "+inf";
    }
    if (number == 0.0) {
        return "0";   // integral, and "-0" is "0" as gawk --posix prints it
    }
    if (std::trunc(number) == number) {
        // Every digit of the exact value, as "%.0f" prints it, whatever
        // CONVFMT or OFMT says.
        PrintfSpec spec;
        spec.precision = 0;
        spec.conversion = 'f';
        return FormatPrintfFloat(spec, number);
    }
    return FormatAwkNumber(format, number);
}

std::string FormatAwkNumber(const std::string& format, double number) {
    std::string out;
    size_t pos = 0;
    while (pos < format.size()) {
        const size_t percent = format.find('%', pos);
        if (percent == std::string::npos) {
            out.append(format, pos, std::string::npos);
            break;
        }
        out.append(format, pos, percent - pos);
        PrintfSpec spec;
        size_t after = percent;
        if (!ParsePrintfSpec(format, after, spec)) {
            // The format ends inside the specification: as written.
            out.append(format, percent, std::string::npos);
            return out;
        }
        // A '*' width or precision counts as absent (gawk: a fatal error).
        if (spec.widthFromArgument) {
            spec.width.reset();
        }
        if (spec.precisionFromArgument) {
            spec.precision.reset();
        }
        switch (spec.conversion) {
            case '%':
                out += '%';
                break;
            case 'd':
            case 'i':
                out += FormatPrintfSigned(spec, TruncateToIntmax(number));
                break;
            case 'o':
            case 'u':
            case 'x':
            case 'X':
                // A negative value through intmax_t, as glibc's printf
                // converts it: two's complement.
                out += FormatPrintfUnsigned(spec,
                    static_cast<uintmax_t>(TruncateToIntmax(number)));
                break;
            case 'c': {
                const char byte = static_cast<char>(
                    static_cast<unsigned char>(TruncateToIntmax(number)));
                out += FormatPrintfString(spec, std::string_view(&byte, 1));
                break;
            }
            case 'e': case 'E': case 'f': case 'F':
            case 'g': case 'G': case 'a': case 'A':
                out += FormatPrintfFloat(spec, number);
                break;
            default:
                // Not a conversion applied to a number: as written.
                out.append(format, percent, after - percent);
                break;
        }
        pos = after;
    }
    return out;
}

// --- Comparison ---

int CompareValues(const Value& a, const Value& b, const std::string& convfmt) {
    if (a.IsNumeric() && b.IsNumeric()) {
        const double x = a.ToNumber();
        const double y = b.ToNumber();
        if (std::isnan(x) || std::isnan(y)) {
            return kAwkUnordered;
        }
        if (x < y) {
            return -1;
        }
        if (x > y) {
            return 1;
        }
        return 0;
    }
    // Byte by byte, as unsigned chars (std::string::compare compares with
    // char_traits, memcmp's semantics).
    const int order = a.ToString(convfmt).compare(b.ToString(convfmt));
    return order < 0 ? -1 : order > 0 ? 1 : 0;
}

bool IsAwkIdentifier(std::string_view name) {
    if (name.empty()) {
        return false;
    }
    const auto isLetter = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    };
    if (!isLetter(name[0])) {
        return false;
    }
    for (const char c : name.substr(1)) {
        if (!isLetter(c) && !(c >= '0' && c <= '9')) {
            return false;
        }
    }
    return true;
}

// --- AwkArray ---

Value* AwkArray::Find(const std::string& key) {
    const auto it = m_index.find(key);
    return it == m_index.end() ? nullptr : &it->second->second;
}

Value& AwkArray::GetOrCreate(const std::string& key) {
    const auto it = m_index.find(key);
    if (it != m_index.end()) {
        return it->second->second;
    }
    auto pair = m_index.emplace(key, m_pairs.emplace(m_pairs.end(), key, Value()));
    return pair.first->second->second;
}

bool AwkArray::Contains(const std::string& key) const {
    return m_index.find(key) != m_index.end();
}

void AwkArray::Remove(const std::string& key) {
    const auto it = m_index.find(key);
    if (it == m_index.end()) {
        return;
    }
    m_pairs.erase(it->second);
    m_index.erase(it);
}

void AwkArray::Clear() {
    m_pairs.clear();
    m_index.clear();
}

size_t AwkArray::Size() const {
    return m_pairs.size();
}

std::vector<std::string> AwkArray::Keys() const {
    std::vector<std::string> keys;
    keys.reserve(m_pairs.size());
    for (const auto& pair : m_pairs) {
        keys.push_back(pair.first);
    }
    return keys;
}

// --- AwkIntegerOf ---

intmax_t AwkIntegerOf(double number) {
    if (std::isnan(number)) {
        return std::numeric_limits<intmax_t>::min();
    }
    const double truncated = std::trunc(number);
    // 2^63 is exactly representable as a double; intmax_t's own top is not,
    // so anything at or above it, and anything below -2^63, is out of range.
    constexpr double kRange = 9223372036854775808.0;
    if (truncated < -kRange || truncated >= kRange) {
        return std::numeric_limits<intmax_t>::min();
    }
    return static_cast<intmax_t>(truncated);
}

} // namespace Haisos::Awk