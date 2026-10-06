#include "BuiltinCommand.h"

#include "commands/hsh/HshBuiltins.h"
#include "commands/hsh/HshError.h"
#include "commands/hsh/HshInvocation.h"
#include "commands/hsh/HshShell.h"

namespace Haisos::Hsh {
const char* HshVersion() { return "0.5.0"; }
} // namespace Haisos::Hsh

namespace Haisos {

namespace {

// Ids of the treated options; the table is used only for --help and by the
// generic builtin tests, since Run parses the arguments itself (dash's '+x',
// '-o name' and options ending at the first operand are not getopt_long's).
enum {
    kOptionAllexport = 1,
    kOptionCommandString,
    kOptionNoclobber,
    kOptionErrexit,
    kOptionNoglob,
    kOptionInteractive,
    kOptionNoexec,
    kOptionNamedOption,
    kOptionStdin,
    kOptionNounset,
    kOptionXtrace,
};

class HshCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "hsh"; }
    std::string Version() const override { return Hsh::HshVersion(); }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'a', "", kOptionAllexport, BuiltinArgument::None, "", "allexport: export every variable assigned"},
            {'b', "", kBuiltinNotTreated},
            {'c', "", kOptionCommandString, BuiltinArgument::None, "", "read commands from the command_string operand"},
            {'C', "", kOptionNoclobber, BuiltinArgument::None, "", "noclobber: > does not overwrite an existing file"},
            {'e', "", kOptionErrexit, BuiltinArgument::None, "", "errexit: exit when an untested command fails"},
            {'E', "", kBuiltinNotTreated},
            {'f', "", kOptionNoglob, BuiltinArgument::None, "", "noglob: no pathname expansion"},
            {'i', "", kOptionInteractive, BuiltinArgument::None, "", "interactive: prompt for commands"},
            {'I', "", kBuiltinNotTreated},
            {'l', "", kBuiltinNotTreated},
            {'m', "", kBuiltinNotTreated},
            {'n', "", kOptionNoexec, BuiltinArgument::None, "", "noexec: read commands without running them"},
            {'o', "", kOptionNamedOption, BuiltinArgument::Required, "NAME", "turn on the option NAME (+o: off)"},
            {'p', "", kBuiltinNotTreated},
            {'s', "", kOptionStdin, BuiltinArgument::None, "", "stdin: read commands from standard input"},
            {'u', "", kOptionNounset, BuiltinArgument::None, "", "nounset: expanding an unset variable is an error"},
            {'v', "", kBuiltinNotTreated},
            {'V', "", kBuiltinNotTreated},
            {'x', "", kOptionXtrace, BuiltinArgument::None, "", "xtrace: show each command before running it"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        BuiltinHelp help;
        help.summary = "command interpreter (shell)";
        help.usage = {
            "hsh [-aCefnuvxIimVEbp] [+aCefnuvxIimVEbp] [-o option_name] [+o option_name] [command_file [argument ...]]",
            "hsh -c [-aCefnuvxIimVEbp] [+aCefnuvxIimVEbp] [-o option_name] [+o option_name] command_string [command_name [argument ...]]",
            "hsh -s [-aCefnuvxIimVEbp] [+aCefnuvxIimVEbp] [-o option_name] [+o option_name] [argument ...]",
        };
        help.notes =
            "Commands not built into hsh are looked up in PATH and started as Haisos processes (builtins, .md agents, .lua scripts).\n"
            "--help and --version only as the first argument; $0 is \"hsh\" unless a script or -c command_name names it; +X turns option X off. PS4 is not expanded; -v is accepted, not acted on.\n"
            "A background (&) builtin, function or compound command runs in a child hsh, which sees only exported variables.\n";
        help.basedOn = "dash";
        return help;
    }

    int Run(BuiltinContext& context) override {
        const auto& args = context.Args();
        // As dash: --help / --version count only as the very first argument.
        if (!args.empty() && args[0] == "--help") {
            context.Out(BuiltinHelpText(*this));
            return 0;
        }
        if (!args.empty() && args[0] == "--version") {
            context.Out(BuiltinVersionText(*this));
            return 0;
        }
        Hsh::Invocation invocation = Hsh::ParseInvocation(args);
        for (const std::string& spelling : invocation.notTreated) {
            context.NotTreated(spelling);
        }
        if (!invocation.error.empty()) {
            context.ErrorText(Hsh::FormatShellError("hsh", 0, invocation.error));
            return 2;
        }
        Hsh::Shell shell(context, std::move(invocation));
        return shell.Run();
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateHshCommand() {
    return std::make_shared<HshCommand>();
}

} // namespace Haisos
