#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinPrintf.h"
#include "BuiltinText.h"

namespace Haisos {

namespace {

// The largest increment the decimal-string fast path takes (GNU's
// SEQ_FAST_STEP_LIMIT).
constexpr long double kFastStepLimit = 200;

enum SeqOption { kFormat = 1, kSeparator, kEqualWidth };

// One operand as GNU's scan_arg reads it: its value, and the width and
// precision of the string that wrote it, for choosing a format.
struct SeqOperand {
    long double value = 1;
    long long width = 1;
    int precision = 0;
};

// strtold consuming the whole string (GNU's xstrtold): leading blanks are
// fine, anything left over is not. An underflow (strtold's ERANGE with a
// zero result) is accepted; an overflow (ERANGE with infinity) is not, so
// `seq 1e5000` is an invalid argument, as in GNU, never an endless sequence.
bool StrtoldAll(const std::string& text, long double& value) {
    errno = 0;
    char* end = nullptr;
    value = std::strtold(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0') {
        return false;
    }
    return !(value != 0 && errno == ERANGE);
}

// GNU's scan_arg: the value of one operand, and the width and precision of
// the string that wrote it. Reports and returns nullopt when the operand is
// not a number at all, a NaN, or (checked by the caller) a zero increment.
std::optional<SeqOperand> ScanArg(BuiltinContext& context, const std::string& arg) {
    SeqOperand ret;
    ret.value = 0;
    if (!StrtoldAll(arg, ret.value)) {
        context.Error("invalid floating point argument: " + GnuQuote(arg));
        context.TryHelp();
        return std::nullopt;
    }
    if (std::isnan(ret.value)) {
        context.Error("invalid " + GnuQuote("not-a-number") + " argument: " + GnuQuote(arg));
        context.TryHelp();
        return std::nullopt;
    }

    // Width and precision of the string, blanks and a leading '+' skipped
    // (they are not printed back). Defaults: auto (INT_MAX) precision, no
    // width; an integer (no '.', not a hex float's 'p') has precision 0.
    size_t start = 0;
    while (start < arg.size()
        && (arg[start] == ' ' || arg[start] == '\t' || arg[start] == '\n'
            || arg[start] == '\v' || arg[start] == '\f' || arg[start] == '\r' || arg[start] == '+')) {
        ++start;
    }
    const std::string text = arg.substr(start);
    ret.width = 0;
    ret.precision = INT_MAX;
    const size_t decimalPoint = text.find('.');
    if (decimalPoint == std::string::npos && text.find('p') == std::string::npos) {
        ret.precision = 0;
    }
    if (text.find_first_of("xX") == std::string::npos && std::isfinite(ret.value)) {
        long long fractionLen = 0;
        ret.width = static_cast<long long>(text.size());
        if (decimalPoint != std::string::npos) {
            size_t e = text.find_first_of("eE", decimalPoint + 1);
            fractionLen = e == std::string::npos
                ? static_cast<long long>(text.size() - decimalPoint - 1)
                : static_cast<long long>(e - decimalPoint - 1);
            if (fractionLen <= INT_MAX) {
                ret.precision = static_cast<int>(fractionLen);
            }
            // ".5" is written back "0.5", "5." is written back "5".
            ret.width += fractionLen == 0
                ? -1
                : (decimalPoint == 0 || text[decimalPoint - 1] < '0' || text[decimalPoint - 1] > '9')
                    ? 1 : 0;
        }
        size_t e = text.find('e');
        if (e == std::string::npos) {
            e = text.find('E');
        }
        if (e != std::string::npos) {
            long long exponent = std::strtoll(text.c_str() + e + 1, nullptr, 10);
            if (exponent < -LONG_MAX) {
                exponent = -LONG_MAX;
            }
            ret.precision = static_cast<int>(static_cast<long long>(ret.precision)
                + (exponent < 0 ? -exponent : -std::min<long long>(ret.precision, exponent)));
            // The exponent is not printed back; the width follows what is.
            ret.width -= static_cast<long long>(text.size() - e);
            if (exponent < 0) {
                if (decimalPoint != std::string::npos) {
                    if (e == decimalPoint + 1) {
                        ret.width += 1;
                    }
                } else {
                    ret.width += 1;
                }
                exponent = -exponent;
            } else {
                if (decimalPoint != std::string::npos && ret.precision == 0 && fractionLen > 0) {
                    ret.width -= 1;
                }
                exponent -= std::min<long long>(fractionLen, exponent);
            }
            ret.width += exponent;
        }
    }
    return ret;
}

// GNU's get_default_format: %.PRECf when every operand is a fixed-point
// decimal, padded to the widest when -w, else %Lg. The width of an operand
// is stretched or shrunk to the precision the others ask for.
std::string GetDefaultFormat(const SeqOperand& first, const SeqOperand& step,
                             const SeqOperand& last, bool equalWidth) {
    const long long prec = std::max(first.precision, step.precision);
    if (prec != INT_MAX && last.precision != INT_MAX) {
        if (equalWidth) {
            long long firstWidth = first.width + (prec - first.precision);
            long long lastWidth = last.width + (prec - last.precision);
            if (last.precision != 0 && prec == 0) {
                lastWidth -= 1; // the '.' of LAST is not printed back
            }
            if (last.precision == 0 && prec != 0) {
                lastWidth += 1; // a '.' must be added to LAST
            }
            if (first.precision == 0 && prec != 0) {
                firstWidth += 1; // a '.' must be added to FIRST
            }
            const long long width = std::max(firstWidth, lastWidth);
            if (width >= 0 && width <= INT_MAX) {
                return "%0" + std::to_string(width) + "." + std::to_string(prec) + "Lf";
            }
        } else {
            return "%." + std::to_string(prec) + "Lf";
        }
    }
    return "%Lg";
}

// A -f format after GNU's long_double_format: the literal texts around the
// conversion (with %% collapsed, as they print) and the conversion itself.
struct SeqFormat {
    std::string prefix;
    std::string suffix;
    PrintfSpec spec;
};

// GNU's long_double_format: one floating-point conversion, of e f g a E F G A,
// with flags, width, precision and an optional L; %% pairs around it; nothing
// else. Reports and returns nullopt on any other format.
std::optional<SeqFormat> LongDoubleFormat(BuiltinContext& context, const std::string& format) {
    // The printed prefix: text with %% collapsed, up to the first lone %.
    size_t i = 0;
    std::string prefix;
    while (!(i < format.size() && format[i] == '%'
        && !(i + 1 < format.size() && format[i + 1] == '%'))) {
        if (i >= format.size()) {
            context.Error("format " + GnuQuote(format) + " has no % directive");
            return std::nullopt;
        }
        prefix += format[i];
        i += format[i] == '%' ? 2 : 1; // a %% pair prints one %
    }
    const size_t directive = i; // the '%' of the conversion
    ++i;
    while (i < format.size() && (format[i] == '-' || format[i] == '+' || format[i] == '#'
        || format[i] == '0' || format[i] == ' ' || format[i] == '\'')) {
        ++i;
    }
    while (i < format.size() && format[i] >= '0' && format[i] <= '9') {
        ++i;
    }
    if (i < format.size() && format[i] == '.') {
        ++i;
        while (i < format.size() && format[i] >= '0' && format[i] <= '9') {
            ++i;
        }
    }
    const bool hasL = i < format.size() && format[i] == 'L';
    if (hasL) {
        ++i;
    }
    if (i >= format.size()) {
        context.Error("format " + GnuQuote(format) + " ends in %");
        return std::nullopt;
    }
    const char conversion = format[i];
    if (conversion != 'e' && conversion != 'f' && conversion != 'g' && conversion != 'a'
        && conversion != 'E' && conversion != 'F' && conversion != 'G' && conversion != 'A') {
        context.Error("format " + GnuQuote(format) + " has unknown %" + std::string(1, conversion)
            + " directive");
        return std::nullopt;
    }
    // The printed suffix: the rest with %% collapsed; a second lone % is one
    // directive too many.
    std::string suffix;
    ++i;
    while (i < format.size()) {
        if (format[i] == '%' && !(i + 1 < format.size() && format[i + 1] == '%')) {
            context.Error("format " + GnuQuote(format) + " has too many % directives");
            return std::nullopt;
        }
        suffix += format[i];
        i += format[i] == '%' ? 2 : 1;
    }

    SeqFormat result;
    result.prefix = std::move(prefix);
    result.suffix = std::move(suffix);
    size_t pos = directive;
    if (!ParsePrintfSpec(format, pos, result.spec)) {
        // LongDoubleFormat has already accepted the conversion: unreachable.
        context.Error("format " + GnuQuote(format) + " ends in %");
        return std::nullopt;
    }
    return result;
}

// Whether every character is a digit, and there is at least one.
bool AllDigits(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    for (char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

// Compares two decimal strings without leading zeros: the shorter is smaller,
// equal lengths compare as text.
int CompareDigits(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) {
        return left.size() < right.size() ? -1 : 1;
    }
    if (left < right) {
        return -1;
    }
    return left > right ? 1 : 0;
}

// Adds step (at most 200) to a decimal string, in place.
void AddStep(std::string& digits, uintmax_t step) {
    unsigned carry = static_cast<unsigned>(step);
    for (size_t i = digits.size(); carry > 0 && i-- > 0;) {
        const unsigned value = static_cast<unsigned>(digits[i] - '0') + carry;
        digits[i] = static_cast<char>('0' + value % 10);
        carry = value / 10;
    }
    while (carry > 0) {
        digits.insert(digits.begin(), static_cast<char>('0' + carry % 10));
        carry /= 10;
    }
}

// GNU's seq_fast: FIRST to LAST stepping by INCREMENT, in decimal-string
// arithmetic of no fixed size, the separator between numbers and '\n' after
// the last; an endless sequence when LAST is infinite. Returns false when
// FIRST is past LAST (the general path takes over then and prints nothing).
// Checks for a stop on every number, so an endless sequence ends on one.
bool SeqFast(BuiltinContext& context, std::string first, const std::string& last, bool lastInfinite,
             uintmax_t step, const std::string& separator, bool& stopped) {
    auto trimLeadingZeros = [](const std::string& text) {
        size_t start = 0;
        while (start + 1 < text.size() && text[start] == '0') {
            ++start;
        }
        return text.substr(start);
    };
    first = trimLeadingZeros(first);
    const std::string trimmedLast = lastInfinite ? std::string() : trimLeadingZeros(last);
    if (!lastInfinite && CompareDigits(first, trimmedLast) > 0) {
        return false;
    }

    context.Out(first);
    while (true) {
        if (context.StopRequested()) {
            stopped = true;
            return true;
        }
        AddStep(first, step);
        if (!lastInfinite && CompareDigits(first, trimmedLast) > 0) {
            break;
        }
        context.Out(separator);
        context.Out(first);
    }
    context.Out(std::string("\n"));
    return true;
}

// Prints one number with the format's literal texts around it, and returns
// the bytes the conversion alone printed.
std::string FormatSeqNumber(const SeqFormat& format, long double x) {
    return format.prefix + FormatPrintfFloat(format.spec, x) + format.suffix;
}

// GNU's print_numbers: FIRST stepping by INCREMENT while it stays within
// LAST, the separator between numbers, '\n' after the last, and the
// rounding fix: the number just past LAST is still printed when it prints
// as LAST and differently from the number before it (format both, cut the
// prefix and suffix, strtold the middles: compare one with LAST, the two
// middles with each other). Nothing at all when out of range from the start.
void PrintNumbers(BuiltinContext& context, const SeqFormat& format, long double first,
                  long double step, long double last, const std::string& separator,
                  bool& stopped) {
    bool outOfRange = step < 0 ? first < last : last < first;
    if (outOfRange) {
        return;
    }
    long double x = first;
    long double i = 1;
    while (true) {
        if (context.StopRequested()) {
            stopped = true;
            return;
        }
        const long double x0 = x;
        context.Out(FormatSeqNumber(format, x));
        if (outOfRange) {
            break;
        }
        x = first + i * step;
        outOfRange = step < 0 ? x < last : last < x;
        if (outOfRange) {
            // The number just past LAST, as the format would print it: its
            // conversion, strtold-ed, may still equal LAST.
            bool printExtraNumber = false;
            const std::string xStr = FormatSeqNumber(format, x);
            const std::string xMiddle = xStr.substr(format.prefix.size(),
                xStr.size() - format.prefix.size() - format.suffix.size());
            long double xVal = 0;
            if (StrtoldAll(xMiddle, xVal) && xVal == last) {
                const std::string x0Str = FormatSeqNumber(format, x0);
                printExtraNumber = x0Str.substr(format.prefix.size(),
                    x0Str.size() - format.prefix.size() - format.suffix.size()) != xMiddle;
            }
            if (!printExtraNumber) {
                break;
            }
        }
        context.Out(separator);
        i += 1;
    }
    context.Out(std::string("\n"));
}

// Splits the arguments the way GNU seq's option loop does: options only
// before the first operand, a negative number (-5, -.5) an operand too, and
// every word from the first operand on an operand untouched. The option
// words (with an option's argument appended after it) go to |optionWords|
// for BeginBuiltin; the rest are the operands.
void SplitSeqArgs(const std::vector<std::string>& args, std::vector<std::string>& optionWords,
                  std::vector<std::string>& operands) {
    size_t i = 0;
    while (i < args.size()) {
        const std::string& arg = args[i];
        if (arg == "--") {
            ++i; // dropped; everything after it is an operand
            break;
        }
        if (arg.size() < 2 || arg[0] != '-') {
            break; // an operand, a lone "-" included
        }
        if (arg[1] == '.' || (arg[1] >= '0' && arg[1] <= '9')) {
            break; // a negative number
        }
        if (arg.size() > 2 && arg.compare(0, 2, "--") == 0) {
            optionWords.push_back(arg);
            if (arg.find('=') == std::string::npos) {
                // Without '=': --format and --separator (or an unambiguous
                // prefix of either) take the next word as their argument.
                const std::string name = arg.substr(2);
                const bool takesArgument = name == "format" || name == "separator"
                    || std::string_view("format").substr(0, name.size()) == name
                    || std::string_view("separator").substr(0, name.size()) == name;
                if (takesArgument && i + 1 < args.size()) {
                    optionWords.push_back(args[++i]);
                }
            }
            ++i;
            continue;
        }
        // A short cluster: an f or s ends it -- the letters after it are
        // its attached argument, and with none there it takes the next word.
        optionWords.push_back(arg);
        for (size_t j = 1; j < arg.size(); ++j) {
            if (arg[j] == 'f' || arg[j] == 's') {
                if (j + 1 == arg.size() && i + 1 < args.size()) {
                    optionWords.push_back(args[++i]);
                }
                break;
            }
        }
        ++i;
    }
    operands.insert(operands.end(), args.begin() + static_cast<std::ptrdiff_t>(i), args.end());
}

class SeqCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "seq"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'f', "format", kFormat, BuiltinArgument::Required, "FORMAT",
                "printf style floating-point FORMAT"},
            {'s', "separator", kSeparator, BuiltinArgument::Required, "STRING",
                "separate numbers with STRING (default \\n)"},
            {'w', "equal-width", kEqualWidth, BuiltinArgument::None, "",
                "pad with leading zeroes to equal width"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "print a sequence of numbers",
            {"seq [OPTION]... LAST", "seq [OPTION]... FIRST LAST",
             "seq [OPTION]... FIRST INCREMENT LAST"},
            "FIRST or INCREMENT omitted defaults to 1. The sequence ends when the\n"
            "current number plus INCREMENT would pass LAST. FIRST, INCREMENT and\n"
            "LAST are floating point values; INCREMENT must not be 0, none of them\n"
            "may be NaN. The format must suit one argument of type double; it\n"
            "defaults to %.PRECf when all three are fixed point decimals of at\n"
            "most PREC digits, and to %g otherwise. An endless sequence (a LAST of\n"
            "inf) ends when stopped. %a and %A in the format follow the\n"
            "platform's long double.\n"};
    }

    int Run(BuiltinContext& context) override {
        // The options are split out first: ParseBuiltinArgs cannot stop at a
        // negative number, and GNU seq stops taking options at the first
        // operand as well.
        std::vector<std::string> optionWords;
        std::vector<std::string> operands;
        SplitSeqArgs(context.Args(), optionWords, operands);
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, optionWords, *this, /*usageErrorStatus=*/1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }

        std::string formatText;
        bool formatGiven = false;
        std::string separator = "\n";
        bool equalWidth = false;
        for (const auto& option : parsed->options) {
            switch (option.id) {
                case kFormat: formatText = option.argument; formatGiven = true; break;
                case kSeparator: separator = option.argument; break;
                case kEqualWidth: equalWidth = true; break;
                default: break;
            }
        }

        if (operands.empty()) {
            context.Error("missing operand");
            context.TryHelp();
            return 1;
        }
        if (operands.size() > 3) {
            context.Error("extra operand " + GnuQuote(operands[3]));
            context.TryHelp();
            return 1;
        }

        SeqFormat format;
        if (formatGiven) {
            const auto validated = LongDoubleFormat(context, formatText);
            if (!validated) {
                return 1;
            }
            format = *validated;
            if (equalWidth) {
                context.Error("format string may not be specified when printing equal width strings");
                context.TryHelp();
                return 1;
            }
        }

        // The fast path (GNU's seq_fast): all-digits operands, an increment
        // between 1 and 200 when given, no format and no -w, a one-byte
        // separator -- decimal-string arithmetic of no fixed size.
        bool fastStepOk = false;
        if (operands.size() != 3) {
            fastStepOk = true;
        } else {
            long double stepValue = 0;
            if (AllDigits(operands[1]) && StrtoldAll(operands[1], stepValue)
                && 0 < stepValue && stepValue <= kFastStepLimit) {
                fastStepOk = true;
            }
        }
        long double stepValue = 1;
        if (operands.size() == 3) {
            StrtoldAll(operands[1], stepValue);
        }
        if (AllDigits(operands[0])
            && (operands.size() == 1 || AllDigits(operands[1]))
            && (operands.size() < 3 || (fastStepOk && AllDigits(operands[2])))
            && !equalWidth && !formatGiven && separator.size() == 1) {
            const std::string first = operands.size() == 1 ? "1" : operands[0];
            bool stopped = false;
            if (SeqFast(context, first, operands.back(), false,
                    static_cast<uintmax_t>(stepValue), separator, stopped)) {
                return 0;
            }
            // FIRST past LAST: the general path prints nothing for it.
        }

        SeqOperand first = {1, 1, 0};
        SeqOperand step = {1, 1, 0};
        auto last = ScanArg(context, operands[0]);
        if (!last) {
            return 1;
        }
        if (operands.size() >= 2) {
            first = *last;
            last = ScanArg(context, operands[1]);
            if (!last) {
                return 1;
            }
            if (operands.size() == 3) {
                step = *last;
                if (step.value == 0) {
                    context.Error("invalid Zero increment value: " + GnuQuote(operands[1]));
                    context.TryHelp();
                    return 1;
                }
                last = ScanArg(context, operands[2]);
                if (!last) {
                    return 1;
                }
            }
        }

        // The fast path tried again, for integers of the form 1e1, and for a
        // LAST of inf: format FIRST and LAST with %.0Lf and run the printer.
        if (first.precision == 0 && step.precision == 0 && last->precision == 0
            && std::isfinite(first.value) && 0 <= first.value && 0 <= last->value
            && 0 < step.value && step.value <= kFastStepLimit
            && !equalWidth && !formatGiven && separator.size() == 1) {
            PrintfSpec zeroSpec;
            zeroSpec.conversion = 'f';
            zeroSpec.precision = 0;
            const std::string s1 = FormatPrintfFloat(zeroSpec, first.value);
            const std::string s2 = std::isfinite(last->value)
                ? FormatPrintfFloat(zeroSpec, last->value) : "inf";
            if (s1[0] != '-' && s2[0] != '-') {
                bool stopped = false;
                if (SeqFast(context, s1, s2, !std::isfinite(last->value),
                        static_cast<uintmax_t>(step.value), separator, stopped)) {
                    return 0;
                }
            }
        }

        if (!formatGiven) {
            const std::string defaultFormat = GetDefaultFormat(first, step, *last, equalWidth);
            size_t pos = 0;
            if (!ParsePrintfSpec(defaultFormat, pos, format.spec)) {
                return 1; // unreachable: the default format always parses
            }
            format.prefix.clear();
            format.suffix.clear();
        }
        bool stopped = false;
        PrintNumbers(context, format, first.value, step.value, last->value, separator, stopped);
        return 0;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateSeqCommand() {
    return std::make_shared<SeqCommand>();
}

} // namespace Haisos