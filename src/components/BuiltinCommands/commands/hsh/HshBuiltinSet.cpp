#include "commands/hsh/HshBuiltins.h"

#include <string>

#include "commands/hsh/HshInvocation.h"
#include "commands/hsh/HshQuote.h"
#include "commands/hsh/HshShell.h"

namespace Haisos::Hsh {

namespace {

// "Parameter <spelling> is not treated by HaisosOS hsh v. <version>\n" through
// the shell's stderr: BuiltinContext's NotTreated reports once per spelling
// and to the stderr captured at construction, which a `set` inside the shell
// must not use.
void ReportNotTreatedOption(Shell& shell, const std::string& spelling) {
    shell.WriteErr("Parameter " + spelling + " is not treated by HaisosOS hsh v. " + HshVersion() + "\n");
}

// `set -o` with no option name: dash's minus_o listing.
void PrintOptionSettings(Shell& shell) {
    shell.WriteOut("Current option settings\n");
    for (const ShellOptionInfo& info : ShellOptionTable()) {
        std::string row = info.name;  // left-justified to 16, as dash's "%-16s"
        if (row.size() < 16) {
            row.append(16 - row.size(), ' ');
        }
        row += info.field && shell.State().options.*(info.field) ? "on\n" : "off\n";
        shell.WriteOut(row);
    }
}

// `set +o` with no option name: the "set -o"/"set +o" listing.
void PrintOptionRecreation(Shell& shell) {
    for (const ShellOptionInfo& info : ShellOptionTable()) {
        const bool on = info.field && shell.State().options.*(info.field);
        shell.WriteOut(std::string(on ? "set -o " : "set +o ") + info.name + "\n");
    }
}

} // namespace

// set [-/+ options] [args], as dash's setcmd: alone, lists every set variable
// sorted by name ("NAME='value'"); else -X turns an option on and +X off (-o
// takes its name from the next argument and errors "set: Illegal option -o
// <name>" for an unknown one, "+o" too; a missing name lists instead), "-"
// turns -x and -v off and ends the options, "--" ends them and, with nothing
// after, clears the positional parameters; the operands left become the
// positional parameters. Unknown letters are fatal ("set: Illegal option
// -X", set is a special builtin); ones hsh does not act on are reported as
// not treated.
int BuiltinSet(Shell& shell, const std::vector<std::string>& args) {
    ShellState& state = shell.State();
    if (args.size() == 1) {
        for (const std::string& name : state.variables.Names()) {
            const std::optional<std::string> value = state.variables.Get(name);
            if (value) {
                shell.WriteOut(name + "=" + ShellSingleQuote(*value) + "\n");
            }
        }
        return 0;
    }
    size_t i = 1;
    while (i < args.size()) {
        const std::string& arg = args[i++];
        if (arg.empty() || (arg[0] != '-' && arg[0] != '+')) {
            --i;  // the first operand: the options end here
            break;
        }
        const bool on = arg[0] == '-';
        if (on && arg == "-") {
            // A lone "-": -x and -v off, the options end, positionals keep.
            state.options.xtrace = false;
            state.options.verbose = false;
            break;
        }
        if (on && arg == "--") {
            if (i == args.size()) {
                state.positional.clear();  // "set --" alone clears them
            }
            break;
        }
        for (size_t j = 1; j < arg.size(); ++j) {
            const char letter = arg[j];
            if (letter == 'o') {
                if (i < args.size()) {  // the option name is the next argument
                    const std::string name = args[i++];
                    const ShellOptionInfo* info = FindShellOption(name);
                    if (!info) {
                        shell.Fail("set: Illegal option -o " + name);
                    }
                    if (info->field) {
                        state.options.*(info->field) = on;
                    } else {
                        ReportNotTreatedOption(shell, std::string(on ? "-o " : "+o ") + name);
                    }
                } else if (on) {
                    PrintOptionSettings(shell);
                } else {
                    PrintOptionRecreation(shell);
                }
                continue;  // the cluster's remaining letters still parse
            }
            const ShellOptionInfo* info = FindShellOption(letter);
            if (!info) {
                shell.Fail(std::string("set: Illegal option ") + (on ? "-" : "+") + letter);
            }
            if (info->field) {
                state.options.*(info->field) = on;
            } else {
                ReportNotTreatedOption(shell, std::string(on ? "-" : "+") + letter);
            }
        }
    }
    if (i < args.size()) {
        state.positional.assign(args.begin() + static_cast<ptrdiff_t>(i), args.end());
    }
    return 0;
}

} // namespace Haisos::Hsh
