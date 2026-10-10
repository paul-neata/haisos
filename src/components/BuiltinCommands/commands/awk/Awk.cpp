#include "BuiltinCommand.h"
#include "commands/awk/AwkInterpreter.h"
#include "commands/awk/AwkInvocation.h"
#include "commands/awk/AwkParser.h"
#include <memory>
#include <string>
#include <vector>

namespace Haisos {

namespace {

class AwkCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "awk"; }
    std::string Version() const override { return "1.5.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        return Awk::AwkOptionTable();
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "pattern scanning and processing language",
            {"awk [-F fs] [-v var=value] [--] 'program' [file ...]",
             "awk [-F fs] [-v var=value] -f progfile [-f progfile]... [--] [file ...]"},
            "POSIX awk, as gawk --posix runs it.\n"
            "Usage errors print gawk's two usage lines; the option summary is\n"
            "`awk --help', not gawk's own.\n"
            "An `@' is reported as invalid char in expression where gawk reports a\n"
            "syntax error at the next token.\n"
            "A backslash-newline inside a string is a continuation; gawk --posix\n"
            "refuses a physical newline in a string.\n"
            "`break' and `continue' outside a loop are reported once, where gawk\n"
            "prints the message twice.\n"
            "A function name used as a parameter name is reported once, without\n"
            "gawk's second, location-less line.\n"
            "The end of a -f file inside a rule reports `(END OF FILE)' with the\n"
            "caret at column 0; gawk's column varies with the file's ending.\n"
            "`for (k in a)' visits the keys in insertion order; gawk's order is\n"
            "unspecified.\n"
            "At most 1000000 fields may be made by an assignment ($n = v, NF = n);\n"
            "gawk has no such limit.\n"
            "Regular expressions are POSIX EREs, as gawk --posix takes them\n"
            "(no \\y \\w \\s, no \\< \\>); intervals are supported.\n"
            "rand() has its own sequence; srand's seeds and return values are\n"
            "gawk's.\n"
            "%a and %A print the platform's long double (0x8p-3 for 1 on\n"
            "x86-64; gawk prints 0x1p+0).\n"
            "Commands run in hsh (-c), found in PATH.\n"
            "/dev/stdout, /dev/stderr, /dev/stdin and - are awk's own streams\n"
            "(gawk --posix opens the devices).\n"
            "system() and close() of a pipe return the command's exit status as\n"
            "plain gawk does (256 + the signal for a command stopped or\n"
            "broken-piped, also inside the shell), not gawk --posix's values.\n"
            "User-defined functions may nest at most 200 calls deep; gawk has\n"
            "no fixed limit.\n"
            "`split(s, a[i])' is refused (second argument is not an array);\n"
            "gawk makes a[i] a sub-array, an extension.",
            "gawk",
        };
    }

    int Run(BuiltinContext& context) override {
        int status = 0;
        const std::optional<Awk::AwkInvocation> invocation =
            Awk::ParseAwkInvocation(context, *this, status);
        if (!invocation) {
            return status;
        }
        const std::optional<std::vector<Awk::AwkSource>> sources =
            Awk::LoadAwkSources(context, *invocation, status);
        if (!sources) {
            return status;
        }
        Awk::ParseResult parsed = Awk::ParseAwkProgram(std::move(*sources));
        if (!parsed.diagnostics.empty()) {
            context.ErrorText(parsed.diagnostics);
        }
        if (parsed.failed) {
            return 1;
        }
        Awk::Interpreter interpreter(context, std::move(parsed.program), *invocation);
        return interpreter.Run();
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateAwkCommand() {
    return std::make_shared<AwkCommand>();
}

} // namespace Haisos