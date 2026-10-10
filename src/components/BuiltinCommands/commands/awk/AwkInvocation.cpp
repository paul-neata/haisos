#include "commands/awk/AwkInvocation.h"
#include "BuiltinText.h"

namespace Haisos::Awk {

const std::vector<BuiltinOption>& AwkOptionTable() {
    static const std::vector<BuiltinOption> options = {
        // Treated
        {'F', "field-separator", kAwkOptionFieldSeparator, BuiltinArgument::Required, "FS",
         "use FS for the input field separator"},
        {'f', "file", kAwkOptionFile, BuiltinArgument::Required, "PROGFILE",
         "read the program from PROGFILE (repeatable; - is standard input)"},
        {'v', "assign", kAwkOptionAssign, BuiltinArgument::Required, "VAR=VAL",
         "assign VAL to VAR before the program starts"},
        {'h', "", kAwkOptionHelp, BuiltinArgument::None, "",
         "show this help (as --help)"},
        {'V', "", kAwkOptionVersion, BuiltinArgument::None, "",
         "show the version (as --version)"},
        {'P', "posix", kAwkOptionPosix, BuiltinArgument::None, "",
         "POSIX awk (always on)"},
        {'r', "re-interval", kAwkOptionReInterval, BuiltinArgument::None, "",
         "interval expressions in regexes (always on)"},
        // Not treated
        {'b', "characters-as-bytes", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
        {'c', "traditional", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
        {'C', "copyright", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
        {'d', "dump-variables", kBuiltinNotTreated, BuiltinArgument::OptionalAttached, "FILE", ""},
        {'D', "debug", kBuiltinNotTreated, BuiltinArgument::OptionalAttached, "FILE", ""},
        {'e', "source", kBuiltinNotTreated, BuiltinArgument::Required, "PROGRAMTEXT", ""},
        {'E', "exec", kBuiltinNotTreated, BuiltinArgument::Required, "PROGRAMTEXT", ""},
        {'g', "gen-pot", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
        {'i', "include", kBuiltinNotTreated, BuiltinArgument::Required, "FILE", ""},
        {'I', "trace", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
        {'k', "csv", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
        {'l', "load", kBuiltinNotTreated, BuiltinArgument::Required, "FILE", ""},
        {'L', "lint", kBuiltinNotTreated, BuiltinArgument::OptionalAttached, "WHEN", ""},
        {'M', "bignum", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
        {'N', "use-lc-numeric", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
        {'n', "non-decimal-data", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
        {'o', "pretty-print", kBuiltinNotTreated, BuiltinArgument::OptionalAttached, "FILE", ""},
        {'O', "optimize", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
        {'p', "profile", kBuiltinNotTreated, BuiltinArgument::OptionalAttached, "FILE", ""},
        {'s', "no-optimize", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
        {'S', "sandbox", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
        {'t', "lint-old", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
        {'W', "", kBuiltinNotTreated, BuiltinArgument::Required, "VALUE", ""},
    };
    return options;
}

std::string AwkUsageText() {
    return "Usage: awk [POSIX or GNU style options] -f progfile [--] file ...\n"
           "Usage: awk [POSIX or GNU style options] [--] 'program' file ...\n";
}

std::optional<AwkInvocation> ParseAwkInvocation(
    BuiltinContext& context, const IBuiltinCommand& command, int& exitStatus) {
    // gawk reads the command line before the program; everything after the
    // program operand (or the first operand at all) is operands.
    const ParsedBuiltinArgs parsed = ParseBuiltinArgs(context.Args(), AwkOptionTable(), true);
    if (!parsed.error.empty()) {
        context.ErrorText("awk: " + parsed.error + "\n");
        context.ErrorText(AwkUsageText());
        exitStatus = 1;
        return std::nullopt;
    }
    for (const ParsedBuiltinOption& option : parsed.options) {
        if (option.id == kBuiltinOptionHelp || option.id == kAwkOptionHelp) {
            context.Out(BuiltinHelpText(command));
            exitStatus = 0;
            return std::nullopt;
        }
        if (option.id == kBuiltinOptionVersion || option.id == kAwkOptionVersion) {
            context.Out(BuiltinVersionText(command));
            exitStatus = 0;
            return std::nullopt;
        }
    }
    context.ReportNotTreated(parsed);
    AwkInvocation invocation;
    for (const ParsedBuiltinOption& option : parsed.options) {
        switch (option.id) {
            case kAwkOptionFieldSeparator:
                invocation.preAssignments.push_back(AwkPreAssignment{true, option.argument});
                break;
            case kAwkOptionAssign:
                if (option.argument.find('=') == std::string::npos) {
                    // The word is gawk's, whichever way the option was spelled.
                    context.ErrorText("awk: `" + option.argument +
                                      "' argument to `-v' not in `var=value' form\n\n");
                    context.ErrorText(AwkUsageText());
                    exitStatus = 1;
                    return std::nullopt;
                }
                invocation.preAssignments.push_back(AwkPreAssignment{false, option.argument});
                break;
            case kAwkOptionFile:
                invocation.programFiles.push_back(option.argument);
                break;
            default:
                break;  // -P, -r: always on; the rest already reported
        }
    }
    if (invocation.programFiles.empty()) {
        if (parsed.operands.empty()) {
            context.ErrorText(AwkUsageText());
            exitStatus = 1;
            return std::nullopt;
        }
        invocation.programText = parsed.operands.front();
        invocation.operands.assign(parsed.operands.begin() + 1, parsed.operands.end());
    } else {
        invocation.operands = parsed.operands;
    }
    return invocation;
}

std::optional<std::vector<AwkSource>> LoadAwkSources(
    BuiltinContext& context, const AwkInvocation& invocation, int& exitStatus) {
    std::vector<AwkSource> sources;
    if (invocation.programFiles.empty()) {
        sources.push_back(AwkSource{kCommandLineSourceName, invocation.programText.value_or("")});
        return sources;
    }
    for (const std::string& name : invocation.programFiles) {
        InputOpenFailure failure = InputOpenFailure::None;
        std::shared_ptr<IFileDescriptor> file = OpenInputOperand(context, name, failure);
        if (!file) {
            if (failure == InputOpenFailure::Directory) {
                context.ErrorText(FormatAwkError(
                    name, 1, "cannot read source file `" + name + "': Is a directory"));
                exitStatus = 1;
                return std::nullopt;
            }
            context.ErrorText("awk: fatal: cannot open source file `" + name +
                              "' for reading: " + OpenFailureText(failure) + "\n");
            exitStatus = 2;
            return std::nullopt;
        }
        std::string text;
        const WholeReadOutcome outcome = ReadWholeInput(context, *file, text);
        if (outcome == WholeReadOutcome::Stopped) {
            exitStatus = 143;
            return std::nullopt;
        }
        if (outcome == WholeReadOutcome::Error) {
            context.ErrorText("awk: fatal: cannot open source file `" + name +
                              "' for reading: Input/output error\n");
            exitStatus = 2;
            return std::nullopt;
        }
        sources.push_back(AwkSource{name, std::move(text)});
    }
    return sources;
}

} // namespace Haisos::Awk