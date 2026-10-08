#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinPrintf.h"
#include "BuiltinText.h"
#include "src/components/Unicode/Unicode.h"

namespace Haisos {

namespace {

// The whole run of one printf invocation: the arguments left, how many of
// them have been consumed, the exit status so far, and whether the command
// must stop at once (\c, or one of GNU's error(EXIT_FAILURE,...) cases, when
// GNU would exit the program rather than go on to the next pass).
struct PrintfRun {
    const std::vector<std::string>* args = nullptr;
    size_t argIndex = 0;
    int status = 0;
    bool stopAtOnce = false;
};

// The character-constant prologue of GNU's STRTOX parsers: an argument
// starting with ' or " is the character that follows it -- its UTF-8 code
// point when it starts a valid sequence (Haisos is UTF-8 throughout; GNU,
// in a UTF-8 locale, does the same), else that one byte -- and whatever
// follows the character draws GNU's warning. Sets *value and returns true
// when the argument is one.
bool CharacterConstantArg(BuiltinContext& context, const std::string& arg, long double& value) {
    if ((arg[0] != '\'' && arg[0] != '"') || arg.size() < 2) {
        return false;
    }
    size_t length = 1;
    unsigned int code = static_cast<unsigned char>(arg[1]);
    const Unicode::DecodedChar decoded = Unicode::DecodeUtf8(arg.data() + 1, arg.size() - 1);
    if (decoded.status == Unicode::DecodeStatus::Ok) {
        code = decoded.codePoint;
        length = decoded.length;
    }
    value = code;
    if (1 + length < arg.size()) {
        context.Error("warning: " + arg.substr(1 + length)
            + ": character(s) following character constant have been ignored");
    }
    return true;
}

// GNU's verify_numeric: an out-of-range value, an argument that parses to
// nothing, or one with something left over -- each a diagnostic that sets
// the exit status to 1, the value parsed so far still printed.
void VerifyNumeric(BuiltinContext& context, const std::string& arg, const char* end, int& status) {
    if (errno == ERANGE) {
        context.Error(GnuQuote(arg) + ": Numerical result out of range");
        status = 1;
    } else if (*end != '\0') {
        if (end == arg.c_str()) {
            context.Error(GnuQuote(arg) + ": expected a numeric value");
        } else {
            context.Error(GnuQuote(arg) + ": value not completely converted");
        }
        status = 1;
    }
}

// GNU's vstrtoimax: strtoimax with base 0 (0x hex, leading 0 octal, leading
// blanks and a sign allowed). An empty argument is 0, with no message.
intmax_t Vstrtoimax(BuiltinContext& context, const std::string& arg, int& status) {
    long double constant = 0;
    if (CharacterConstantArg(context, arg, constant)) {
        return static_cast<intmax_t>(constant);
    }
    errno = 0;
    char* end = nullptr;
    const long long value = std::strtoll(arg.c_str(), &end, 0);
    VerifyNumeric(context, arg, end, status);
    return static_cast<intmax_t>(value);
}

// GNU's vstrtoumax: strtoumax with base 0. %u of -1 is
// 18446744073709551615, as strtoumax (not strtoll) gives it.
uintmax_t Vstrtoumax(BuiltinContext& context, const std::string& arg, int& status) {
    long double constant = 0;
    if (CharacterConstantArg(context, arg, constant)) {
        return static_cast<uintmax_t>(constant);
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(arg.c_str(), &end, 0);
    VerifyNumeric(context, arg, end, status);
    return static_cast<uintmax_t>(value);
}

// GNU's vstrtold: strtold in the C locale (hex floats, inf and nan).
long double Vstrtold(BuiltinContext& context, const std::string& arg, int& status) {
    long double constant = 0;
    if (CharacterConstantArg(context, arg, constant)) {
        return constant;
    }
    errno = 0;
    char* end = nullptr;
    const long double value = std::strtold(arg.c_str(), &end);
    VerifyNumeric(context, arg, end, status);
    return value;
}

// GNU printf's ok table: which conversions may follow the flags given.
bool ConversionAllowed(const PrintfSpec& spec) {
    bool ok[256] = {};
    for (const char* allowed = "aAcdeEfFgGiosuxX"; *allowed; ++allowed) {
        ok[static_cast<unsigned char>(*allowed)] = true;
    }
    for (char flag : spec.flags) {
        switch (flag) {
            case '\'':
            case 'I':
                for (const char* forbidden = "aAceEosxX"; *forbidden; ++forbidden) {
                    ok[static_cast<unsigned char>(*forbidden)] = false;
                }
                break;
            case '#':
                for (const char* forbidden = "cdisu"; *forbidden; ++forbidden) {
                    ok[static_cast<unsigned char>(*forbidden)] = false;
                }
                break;
            case '0':
                ok['c'] = ok['s'] = false;
                break;
            default:
                break; // - + and space forbid nothing
        }
    }
    if (spec.precision.has_value() || spec.precisionFromArgument) {
        ok['c'] = false; // a precision forbids %c
    }
    return ok[static_cast<unsigned char>(spec.conversion)];
}

// One argument under its conversion, the value parsed the way GNU parses it.
std::string FormatDirective(BuiltinContext& context, const PrintfSpec& spec,
                            const std::string& arg, int& status) {
    switch (spec.conversion) {
        case 'd':
        case 'i':
            return FormatPrintfSigned(spec, Vstrtoimax(context, arg, status));
        case 'o':
        case 'u':
        case 'x':
        case 'X':
            return FormatPrintfUnsigned(spec, Vstrtoumax(context, arg, status));
        case 'a':
        case 'A':
        case 'e':
        case 'E':
        case 'f':
        case 'F':
        case 'g':
        case 'G':
            return FormatPrintfFloat(spec, Vstrtold(context, arg, status));
        case 'c':
            // The first byte of the argument -- an empty argument is one NUL
            // byte, as glibc's %c of '\0' is.
            return FormatPrintfString(spec,
                arg.empty() ? std::string_view("\0", 1) : std::string_view(arg.data(), 1));
        case 's':
            return FormatPrintfString(spec, arg);
        default:
            return "";
    }
}

// One pass of the format over the remaining arguments (GNU's
// print_formatted); returns how many arguments it consumed.
size_t PrintFormatted(BuiltinContext& context, const std::string& format, PrintfRun& run) {
    const size_t argsAtStart = run.argIndex;
    std::string out;
    size_t i = 0;
    while (i < format.size()) {
        const char c = format[i];
        if (c == '\\') {
            std::string error;
            const PrintfEscapeResult result = AppendPrintfEscape(format, i, false, out, error);
            if (result == PrintfEscapeResult::Stop) {
                // \c: nothing more may be output -- not the rest of the
                // format, not the rest of the arguments. GNU exits with
                // EXIT_SUCCESS.
                context.Out(out);
                run.status = 0;
                run.stopAtOnce = true;
                return run.argIndex - argsAtStart;
            }
            if (result == PrintfEscapeResult::Error) {
                context.Out(out);
                context.Error(error);
                run.status = 1;
                run.stopAtOnce = true;
                return run.argIndex - argsAtStart;
            }
            continue;
        }
        if (c != '%') {
            out += c;
            ++i;
            continue;
        }
        // A directive. Only exactly %b and %q: anything before the letter
        // (a flag, a width) makes them ordinary conversions, and %b with
        // one is an invalid specification, as in GNU.
        if (i + 1 < format.size() && format[i + 1] == '%') {
            out += '%';
            i += 2;
            continue;
        }
        if (i + 1 < format.size() && format[i + 1] == 'b') {
            i += 2;
            if (run.argIndex < run.args->size()) {
                const std::string& arg = (*run.args)[run.argIndex++];
                size_t j = 0;
                while (j < arg.size()) {
                    if (arg[j] != '\\') {
                        out += arg[j];
                        ++j;
                        continue;
                    }
                    std::string error;
                    const PrintfEscapeResult result = AppendPrintfEscape(arg, j, true, out, error);
                    if (result == PrintfEscapeResult::Stop) {
                        context.Out(out);
                        run.status = 0;
                        run.stopAtOnce = true;
                        return run.argIndex - argsAtStart;
                    }
                    if (result == PrintfEscapeResult::Error) {
                        context.Out(out);
                        context.Error(error);
                        run.status = 1;
                        run.stopAtOnce = true;
                        return run.argIndex - argsAtStart;
                    }
                }
            }
            continue;
        }
        if (i + 1 < format.size() && format[i + 1] == 'q') {
            i += 2;
            if (run.argIndex < run.args->size()) {
                out += ShellEscapeQuoted((*run.args)[run.argIndex++]);
            }
            continue;
        }
        const size_t start = i;
        PrintfSpec spec;
        if (!ParsePrintfSpec(format, i, spec) || !ConversionAllowed(spec)) {
            // The specification as it stands, from '%' through the
            // character after the length modifiers -- what is there, when
            // the format ends inside it.
            context.Out(out);
            context.Error(format.substr(start, i - start) + ": invalid conversion specification");
            run.status = 1;
            run.stopAtOnce = true;
            return run.argIndex - argsAtStart;
        }
        if (spec.widthFromArgument) {
            intmax_t width = 0;
            if (run.argIndex < run.args->size()) {
                width = Vstrtoimax(context, (*run.args)[run.argIndex++], run.status);
                if (width < INT_MIN || width > INT_MAX) {
                    context.Out(out);
                    context.Error("invalid field width: " + GnuQuote((*run.args)[run.argIndex - 1]));
                    run.status = 1;
                    run.stopAtOnce = true;
                    return run.argIndex - argsAtStart;
                }
            }
            spec.width = static_cast<int>(width);
        }
        if (spec.precisionFromArgument) {
            intmax_t precision = 0;
            if (run.argIndex < run.args->size()) {
                precision = Vstrtoimax(context, (*run.args)[run.argIndex++], run.status);
                if (precision > INT_MAX) {
                    context.Out(out);
                    context.Error("invalid precision: " + GnuQuote((*run.args)[run.argIndex - 1]));
                    run.status = 1;
                    run.stopAtOnce = true;
                    return run.argIndex - argsAtStart;
                }
                // A negative precision is as if none were given.
            }
            spec.precision = static_cast<int>(precision);
        }
        const std::string value = run.argIndex < run.args->size()
            ? (*run.args)[run.argIndex++] : std::string();
        out += FormatDirective(context, spec, value, run.status);
    }
    context.Out(out);
    return run.argIndex - argsAtStart;
}

class PrintfCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "printf"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        // GNU printf takes no options but --help and --version.
        static const std::vector<BuiltinOption> options;
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "format and print data",
            {"printf FORMAT [ARGUMENT]...", "printf OPTION"},
            "FORMAT is reused until all ARGUMENTs are used. \\\\ \\a \\b \\c (stop)\n"
            "\\e \\f \\n \\r \\t \\v \\0NNN \\xHH \\uHHHH \\UHHHHHHHH escapes; the\n"
            "conversions a A c d e E f F g G i o s u x X with * widths and\n"
            "precisions; %b interprets the escapes in its argument, %q quotes it\n"
            "for the shell. A numeric argument is decimal, octal (010), hex\n"
            "(0x1F) or a character constant ('A); an invalid one is a warning,\n"
            "not a failure.\n"
            "--help and --version count only as the sole argument.\n"
            "%q keeps bytes >= 0x80 as they are, where GNU's shell-escape\n"
            "quoting writes an invalid UTF-8 byte as $'\\200'. \\u and \\U always\n"
            "write UTF-8. %a and %A follow the platform's long double.\n"};
    }

    int Run(BuiltinContext& context) override {
        const auto& args = context.Args();
        // Not BeginBuiltin: GNU printf parses its arguments by hand, and
        // takes --help and --version only as the sole argument.
        if (args.size() == 1 && args[0] == "--help") {
            context.Out(BuiltinHelpText(*this));
            return 0;
        }
        if (args.size() == 1 && args[0] == "--version") {
            context.Out(BuiltinVersionText(*this));
            return 0;
        }
        std::vector<std::string> rest = args;
        if (!rest.empty() && rest.front() == "--") {
            rest.erase(rest.begin());
        }
        if (rest.empty()) {
            context.Error("missing operand");
            context.TryHelp();
            return 1;
        }
        const std::string format = rest.front();

        PrintfRun run;
        run.args = &rest;
        // rest[0] is the format; the arguments are everything after it.
        run.argIndex = 1;
        size_t used = 0;
        do {
            used = PrintFormatted(context, format, run);
        } while (!run.stopAtOnce && used > 0 && run.argIndex < rest.size());
        if (!run.stopAtOnce && run.argIndex < rest.size()) {
            context.Error("warning: ignoring excess arguments, starting with "
                + GnuQuote(rest[run.argIndex]));
        }
        return run.status;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreatePrintfCommand() {
    return std::make_shared<PrintfCommand>();
}

} // namespace Haisos