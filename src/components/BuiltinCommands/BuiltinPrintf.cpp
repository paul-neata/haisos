#include "BuiltinPrintf.h"
#include <cstdio>
#include <limits>

namespace Haisos {

namespace {

int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool IsFlag(char c) {
    return c == '-' || c == '+' || c == ' ' || c == '#' || c == '0' || c == '\'' || c == 'I';
}

bool IsLengthModifier(char c) {
    // The modifiers GNU printf skips; 'q' is not one of them -- GNU printf's
    // %q is the quoting conversion, never a modifier.
    return c == 'h' || c == 'l' || c == 'L' || c == 'j' || c == 'z' || c == 't';
}

// A run of digits as a width or precision, clamped to INT_MAX: a wider one
// asks for more output than any caller could mean (GNU hands the digits to
// snprintf, which fails for widths that large anyway).
int ParseDigitRun(std::string_view format, size_t& pos) {
    long long value = 0;
    while (pos < format.size() && format[pos] >= '0' && format[pos] <= '9') {
        if (value <= (std::numeric_limits<int>::max() - 9) / 10) {
            value = value * 10 + (format[pos] - '0');
        } else {
            value = std::numeric_limits<int>::max();
        }
        ++pos;
    }
    return static_cast<int>(value);
}

// The C format rebuilt from the spec: the flags filtered to "-+ #0" (' and I
// dropped: MSVC rejects them), the width (a negative one, from '*', is
// glibc's '-' flag with its absolute value), the precision (a negative one,
// from '*', is none at all), then the length modifier and conversion given.
std::string BuildCFormat(const PrintfSpec& spec, const char* length, char conversion) {
    std::string flags;
    for (char flag : spec.flags) {
        if (flag != '\'' && flag != 'I') {
            flags += flag;
        }
    }
    long long width = 0;
    if (spec.width) {
        width = *spec.width;
        if (width < 0) {
            width = -width;
            if (flags.find('-') == std::string::npos) {
                flags += '-';
            }
        }
    }
    std::string format = "%";
    format += flags;
    if (spec.width) {
        format += std::to_string(width);
    }
    if (spec.precision && *spec.precision >= 0) {
        format += ".";
        format += std::to_string(*spec.precision);
    }
    format += length;
    format += conversion;
    return format;
}

// One snprintf pass, sized then filled. The rebuilt formats above hold no
// conversion the caller did not choose, so the only failure is a negative
// return, which never happens for them.
template <typename T>
std::string SnprintfFormat(const std::string& format, T value) {
    const int length = std::snprintf(nullptr, 0, format.c_str(), value);
    if (length <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(length) + 1, '\0');
    std::snprintf(&out[0], out.size(), format.c_str(), value);
    out.resize(static_cast<size_t>(length));
    return out;
}

// A code point as UTF-8 (GNU's print_unicode_char: \u and \U always write
// UTF-8, the C locale's MB_CUR_MAX of 1 notwithstanding).
void AppendUtf8(unsigned int codePoint, std::string& out) {
    if (codePoint < 0x80) {
        out += static_cast<char>(codePoint);
    } else if (codePoint < 0x800) {
        out += static_cast<char>(0xC0 | (codePoint >> 6));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    } else if (codePoint < 0x10000) {
        out += static_cast<char>(0xE0 | (codePoint >> 12));
        out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    } else {
        // The 4-byte form reaches U+10FFFF; above it, the old 5- and
        // 6-byte forms of UTF-8, as gnulib's unicode_to_utf8 writes them.
        const unsigned int length = codePoint < 0x200000 ? 4 : codePoint < 0x4000000 ? 5 : 6;
        out += static_cast<char>(static_cast<unsigned char>(0x100 - (1 << (8 - length)))
            | (codePoint >> (6 * (length - 1))));
        for (unsigned int shift = 6 * (length - 2); ; shift -= 6) {
            out += static_cast<char>(0x80 | ((codePoint >> shift) & 0x3F));
            if (shift == 0) {
                break;
            }
        }
    }
}

} // namespace

bool ParsePrintfSpec(std::string_view format, size_t& pos, PrintfSpec& spec) {
    spec = PrintfSpec{};
    ++pos; // past '%'
    if (pos < format.size() && format[pos] == '%') {
        spec.conversion = '%';
        ++pos;
        return true;
    }
    while (pos < format.size() && IsFlag(format[pos])) {
        spec.flags += format[pos];
        ++pos;
    }
    if (pos < format.size() && format[pos] == '*') {
        spec.widthFromArgument = true;
        ++pos;
    } else if (pos < format.size() && format[pos] >= '0' && format[pos] <= '9') {
        spec.width = ParseDigitRun(format, pos);
    }
    if (pos < format.size() && format[pos] == '.') {
        ++pos;
        if (pos < format.size() && format[pos] == '*') {
            spec.precisionFromArgument = true;
            ++pos;
        } else {
            spec.precision = ParseDigitRun(format, pos); // no digits: 0
        }
    }
    while (pos < format.size() && IsLengthModifier(format[pos])) {
        ++pos;
    }
    if (pos >= format.size()) {
        pos = format.size();
        return false; // the format ended inside the specification
    }
    spec.conversion = format[pos];
    ++pos;
    return true;
}

std::string FormatPrintfSigned(const PrintfSpec& spec, intmax_t value) {
    const char conversion = (spec.conversion == 'd' || spec.conversion == 'i') ? spec.conversion : 'd';
    return SnprintfFormat(BuildCFormat(spec, "ll", conversion), static_cast<long long>(value));
}

std::string FormatPrintfUnsigned(const PrintfSpec& spec, uintmax_t value) {
    const bool exact = spec.conversion == 'o' || spec.conversion == 'u'
        || spec.conversion == 'x' || spec.conversion == 'X';
    const char conversion = exact ? spec.conversion : 'u';
    return SnprintfFormat(BuildCFormat(spec, "ll", conversion), static_cast<unsigned long long>(value));
}

std::string FormatPrintfFloat(const PrintfSpec& spec, long double value) {
    switch (spec.conversion) {
        case 'a': case 'A': case 'e': case 'E':
        case 'f': case 'F': case 'g': case 'G':
            return SnprintfFormat(BuildCFormat(spec, "L", spec.conversion), value);
        default:
            return SnprintfFormat(BuildCFormat(spec, "L", 'g'), value);
    }
}

std::string FormatPrintfString(const PrintfSpec& spec, std::string_view value) {
    // Padded and truncated by hand: %s and %c must pass embedded NUL bytes
    // through (%c of an empty argument is one NUL byte), and the '0' flag
    // pads a string with spaces, as glibc does -- neither goes through
    // snprintf. The precision truncates, a negative one (from '*') is none.
    std::string_view text = value;
    if (spec.precision && *spec.precision >= 0
        && text.size() > static_cast<size_t>(*spec.precision)) {
        text = text.substr(0, static_cast<size_t>(*spec.precision));
    }
    long long width = spec.width ? *spec.width : 0;
    const bool leftAlign = width < 0 || spec.flags.find('-') != std::string::npos;
    if (width < 0) {
        width = -width;
    }
    const size_t pad = static_cast<long long>(text.size()) < width
        ? static_cast<size_t>(width - static_cast<long long>(text.size())) : 0;
    std::string out;
    if (!leftAlign) {
        out.append(pad, ' ');
    }
    out.append(text);
    if (leftAlign) {
        out.append(pad, ' ');
    }
    return out;
}

PrintfEscapeResult AppendPrintfEscape(std::string_view text, size_t& pos, bool octalZero,
                                      std::string& out, std::string& error) {
    size_t p = pos + 1;
    if (p >= text.size()) {
        // A backslash ending the text: the backslash alone.
        out += '\\';
        pos = p;
        return PrintfEscapeResult::Appended;
    }
    const char c = text[p];
    if (c == 'x') {
        ++p;
        int value = 0;
        int digits = 0;
        while (digits < 2 && p < text.size() && HexValue(text[p]) >= 0) {
            value = value * 16 + HexValue(text[p]);
            ++p;
            ++digits;
        }
        if (digits == 0) {
            error = "missing hexadecimal number in escape";
            return PrintfEscapeResult::Error;
        }
        out += static_cast<char>(value);
        pos = p;
        return PrintfEscapeResult::Appended;
    }
    if (c >= '0' && c <= '7') {
        // \NNN, or %b's \0NNN: the 0 skipped, then up to 3 digits -- and
        // \NNN with a first digit other than 0, as in %b too.
        int value = 0;
        if (octalZero && c == '0') {
            ++p;
        }
        int digits = 0;
        while (digits < 3 && p < text.size() && text[p] >= '0' && text[p] <= '7') {
            value = value * 8 + (text[p] - '0');
            ++p;
            ++digits;
        }
        out += static_cast<char>(value);
        pos = p;
        return PrintfEscapeResult::Appended;
    }
    if (c == 'u' || c == 'U') {
        const int wanted = c == 'u' ? 4 : 8;
        unsigned int value = 0;
        ++p;
        for (int digit = 0; digit < wanted; ++digit) {
            if (p >= text.size() || HexValue(text[p]) < 0) {
                error = "missing hexadecimal number in escape";
                return PrintfEscapeResult::Error;
            }
            value = value * 16 + static_cast<unsigned int>(HexValue(text[p]));
            ++p;
        }
        if (value >= 0xd800 && value <= 0xdfff) {
            char message[64];
            std::snprintf(message, sizeof(message), "invalid universal character name \\%c%0*x",
                c, wanted, value);
            error = message;
            return PrintfEscapeResult::Error;
        }
        AppendUtf8(value, out);
        pos = p;
        return PrintfEscapeResult::Appended;
    }
    switch (c) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case 'a': out += '\a'; break;
        case 'b': out += '\b'; break;
        case 'c': pos = p + 1; return PrintfEscapeResult::Stop;
        case 'e': out += '\x1b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'v': out += '\v'; break;
        default:
            // No escape: the backslash and the character, as written.
            out += '\\';
            out += text[p];
            pos = p + 1;
            return PrintfEscapeResult::Appended;
    }
    pos = p + 1;
    return PrintfEscapeResult::Appended;
}

} // namespace Haisos