#include "commands/awk/AwkFormat.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "BuiltinPrintf.h"
#include "commands/awk/AwkError.h"

namespace Haisos::Awk {
namespace {

// The flags a specification may hold, in any order, repeats allowed -- not
// `I': for awk it begins an unknown conversion, it is never a flag.
bool IsAwkPrintfFlag(char c) {
    return c == '-' || c == '+' || c == ' ' || c == '#' || c == '0' || c == '\'';
}

// The length modifiers, refused outright: "`l' is not permitted in POSIX awk
// formats". (`q' is not one of them: it is an unknown conversion, copied.)
bool IsAwkLengthModifier(char c) {
    return c == 'h' || c == 'l' || c == 'L' || c == 'j' || c == 'z' || c == 't';
}

// The conversions that take one argument; anything else is an unknown
// conversion, copied as written.
bool IsAwkConversion(char c) {
    switch (c) {
        case 'c': case 'd': case 'i': case 'o': case 'u': case 'x': case 'X':
        case 'e': case 'E': case 'f': case 'F': case 'g': case 'G':
        case 'a': case 'A': case 's':
            return true;
        default:
            return false;
    }
}

// The fatal when a `*' or a conversion finds no argument left: the whole
// format between ` and ', and below it the caret at the byte that ran out --
// the `*', or the conversion character.
[[noreturn]] void ThrowRanOut(const std::string& format, size_t offset) {
    throw AwkFatal("not enough arguments to satisfy format string\n\t`" + format +
                   "'\n\t" + std::string(offset, ' ') + "^ ran out for this one");
}

// A `*' width or precision: the argument's number truncated toward zero,
// clamped to int's range (a NaN is 0, as awk's own conversions take it).
int StarValueOf(const Value& value) {
    const double number = value.ToNumber();
    if (std::isnan(number)) {
        return 0;
    }
    if (number >= 2147483647.0) {
        return 2147483647;
    }
    if (number <= -2147483647.0) {
        return -2147483647;
    }
    return static_cast<int>(std::trunc(number));
}

// The byte a numeric %c stands for: the integer truncated toward zero, its
// low 8 bits (321 and -191 are both 'A'); an infinity or a NaN is byte 0.
char ByteOfCharacter(double number) {
    if (!std::isfinite(number)) {
        return '\0';
    }
    double reduced = std::fmod(std::trunc(number), 256.0);
    if (reduced < 0) {
        reduced += 256.0;
    }
    return static_cast<char>(static_cast<unsigned char>(static_cast<int>(reduced)));
}

// inf or nan as awk builds the text itself (glibc and MSVC spell them
// differently, so the C library is never left to): a sign -- `-' when the
// sign bit is set, else `+' with the + flag, else a space with the space
// flag, else none -- then the word, uppercase only for E F G A; padded with
// spaces to the width (on the right with `-'; the 0 flag never pads these).
std::string NonFiniteText(const PrintfSpec& spec, double number) {
    std::string text;
    if (std::signbit(number)) {
        text = "-";
    } else if (spec.flags.find('+') != std::string::npos) {
        text = "+";
    } else if (spec.flags.find(' ') != std::string::npos) {
        text = " ";
    }
    std::string word = std::isnan(number) ? "nan" : "inf";
    if (spec.conversion == 'E' || spec.conversion == 'F'
        || spec.conversion == 'G' || spec.conversion == 'A') {
        for (char& c : word) {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    text += word;
    long long width = spec.width ? *spec.width : 0;
    const bool leftAlign = width < 0 || spec.flags.find('-') != std::string::npos;
    if (width < 0) {
        width = -width;
    }
    if (static_cast<long long>(text.size()) < width) {
        const size_t pad = static_cast<size_t>(width - static_cast<long long>(text.size()));
        text.insert(leftAlign ? text.size() : 0, pad, ' ');
    }
    return text;
}

} // namespace

std::string FormatAwkPrintf(const std::string& format, const std::vector<Value>& arguments,
                            const std::string& convfmt) {
    std::string out;
    size_t argumentIndex = 0;
    // The next argument, or the fatal when none is left; |offset| is where
    // the caret points (the `*', or the conversion character).
    const auto nextArgument = [&](size_t offset) -> const Value& {
        if (argumentIndex >= arguments.size()) {
            ThrowRanOut(format, offset);
        }
        return arguments[argumentIndex++];
    };

    size_t pos = 0;
    while (pos < format.size()) {
        const size_t percent = format.find('%', pos);
        if (percent == std::string::npos) {
            out.append(format, pos, std::string::npos);
            break;
        }
        out.append(format, pos, percent - pos);

        // What the specification is, decided on the text itself first: the
        // rules below are awk's, and ParsePrintfSpec's are printf's and
        // differ (it skips the length modifiers and accepts `I' as a flag).
        size_t i = percent + 1;
        while (i < format.size() && IsAwkPrintfFlag(format[i])) {
            ++i;
        }
        bool widthStar = false;
        size_t widthStarOffset = 0;
        if (i < format.size() && format[i] == '*') {
            widthStar = true;
            widthStarOffset = i;
            ++i;
        } else {
            while (i < format.size() && format[i] >= '0' && format[i] <= '9') {
                ++i;
            }
        }
        bool precisionStar = false;
        size_t precisionStarOffset = 0;
        if (i < format.size() && format[i] == '.') {
            ++i;
            if (i < format.size() && format[i] == '*') {
                precisionStar = true;
                precisionStarOffset = i;
                ++i;
            } else {
                while (i < format.size() && format[i] >= '0' && format[i] <= '9') {
                    ++i;
                }
            }
        }
        if (i >= format.size()) {
            // The format ends inside the specification: as written, and the
            // rest of the format with it.
            out.append(format, percent, std::string::npos);
            return out;
        }
        const char c = format[i];
        if (IsAwkLengthModifier(c)) {
            throw AwkFatal(std::string("`") + c + "' is not permitted in POSIX awk formats");
        }
        if (c == '%') {
            // One '%': the flags, width and precision ignored, no argument.
            out += '%';
            pos = i + 1;
            continue;
        }
        if (!IsAwkConversion(c)) {
            // An unknown conversion: the text from the '%' through it as
            // written, no argument used.
            out.append(format, percent, i - percent + 1);
            pos = i + 1;
            continue;
        }

        // A conversion: read it with ParsePrintfSpec, which parses the same
        // text (c is no length modifier, and an `I' never gets this far).
        PrintfSpec spec;
        size_t after = percent;
        ParsePrintfSpec(format, after, spec);
        if (widthStar) {
            // A negative width is the '-' flag (FormatPrintf* take it so),
            // a negative precision no precision.
            spec.width = StarValueOf(nextArgument(widthStarOffset));
        }
        if (precisionStar) {
            spec.precision = StarValueOf(nextArgument(precisionStarOffset));
        }

        if (c == 's') {
            out += FormatPrintfString(spec, nextArgument(i).ToString(convfmt));
        } else if (c == 'c') {
            const Value& value = nextArgument(i);
            char byte;
            if (value.IsNumeric()) {
                // A number -- an input field `65' among them -- is its byte;
                // an unset variable byte 0.
                byte = ByteOfCharacter(value.ToNumber());
            } else {
                // A string is its first byte, or byte 0 when it is empty.
                const std::string text = value.ToString(convfmt);
                byte = text.empty() ? '\0' : text[0];
            }
            // One byte: the precision is ignored (gawk: %.3c of "xyz" is x,
            // %.0c of "x" is x too).
            spec.precision.reset();
            out += FormatPrintfString(spec, std::string_view(&byte, 1));
        } else {
            const double number = nextArgument(i).ToNumber();
            if (!std::isfinite(number)) {
                out += NonFiniteText(spec, number);
            } else if (c == 'd' || c == 'i') {
                const double t = std::trunc(number);
                constexpr double kLow = -9223372036854775808.0;   // -2^63
                constexpr double kHigh = 9223372036854775808.0;   // 2^63
                if (t >= kLow && t < kHigh) {
                    out += FormatPrintfSigned(spec, static_cast<intmax_t>(t));
                } else {
                    // Beyond 64 bits: every digit, as %.0f prints it, with
                    // the same flags and width -- but no '#', which would
                    // add a '.' that gawk does not print.
                    spec.flags.erase(std::remove(spec.flags.begin(), spec.flags.end(), '#'),
                                     spec.flags.end());
                    spec.conversion = 'f';
                    spec.precision = 0;
                    out += FormatPrintfFloat(spec, number);
                }
            } else if (c == 'o' || c == 'u' || c == 'x' || c == 'X') {
                const double t = std::trunc(number);
                constexpr double kLow = -9223372036854775808.0;     // -2^63
                constexpr double kHigh = 18446744073709551616.0;   // 2^64
                if (t >= kLow && t < kHigh) {
                    // A negative t as its 64-bit two's complement.
                    const uintmax_t magnitude = t < 0
                        ? static_cast<uintmax_t>(static_cast<intmax_t>(t))
                        : static_cast<uintmax_t>(t);
                    out += FormatPrintfUnsigned(spec, magnitude);
                } else {
                    // Beyond 64 bits: the value as %g, the same flags, width
                    // and precision.
                    spec.conversion = 'g';
                    out += FormatPrintfFloat(spec, number);
                }
            } else {
                // e E f F g G a A.
                out += FormatPrintfFloat(spec, number);
            }
        }
        pos = after;
    }
    return out;
}

} // namespace Haisos::Awk