#include "commands/hsh/HshBuiltins.h"

#include <limits>
#include <string>

#include "commands/hsh/HshNumber.h"
#include "commands/hsh/HshQuote.h"
#include "commands/hsh/HshShell.h"
#include "commands/hsh/HshWord.h"

namespace Haisos::Hsh {

namespace {

// export / readonly [-p] [name[=value] ...], as dash's exportcmd (its flag is
// VEXPORT or VREADONLY). -p -- and no operands at all -- lists the variables
// carrying the flag, sorted by name, "export PWD='/'" or "readonly Q". A bad
// name is "export: <name>: bad variable name"; setting a read-only one
// "export: <name>: is read only". Both are special builtins: the errors are
// fatal (Shell::Fail).
int BuiltinExportReadonly(Shell& shell, const std::vector<std::string>& args, bool setReadonly) {
    const std::string command = setReadonly ? "readonly" : "export";
    ShellVariables& variables = shell.State().variables;
    bool list = args.size() == 1;
    size_t i = 1;
    for (; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--") {
            ++i;
            break;
        }
        if (arg.empty() || arg[0] != '-' || arg.size() == 1) {
            break;  // "-" alone and everything else is an operand
        }
        for (size_t j = 1; j < arg.size(); ++j) {
            if (arg[j] != 'p') {
                shell.Fail(command + ": Illegal option -" + std::string(1, arg[j]));
            }
        }
        list = true;  // With -p the listing shows, operands or not, as dash.
    }
    if (list) {
        for (const std::string& name : variables.Names()) {
            if (setReadonly ? !variables.IsReadonly(name) : !variables.IsExported(name)) {
                continue;
            }
            const std::optional<std::string> value = variables.Get(name);
            shell.WriteOut(value ? command + " " + name + "=" + ShellSingleQuote(*value) + "\n"
                                 : command + " " + name + "\n");
        }
        return 0;
    }
    for (; i < args.size(); ++i) {
        const std::string& operand = args[i];
        const size_t equals = operand.find('=');
        const std::string name = operand.substr(0, equals);
        if (!IsValidShellName(name)) {
            shell.Fail(command + ": " + name + ": bad variable name");
        }
        if (equals == std::string::npos) {
            // A bare name: only the flag changes (an existing read-only one
            // keeps its value; an unknown one lists from now on), as dash's
            // "vp->flags |= flag".
            if (setReadonly) {
                variables.MakeReadonly(name);
            } else {
                variables.Export(name);
            }
            continue;
        }
        if (variables.IsReadonly(name)) {
            shell.Fail(command + ": " + name + ": is read only");
        }
        shell.AssignVariable(name, operand.substr(equals + 1));
        if (setReadonly) {
            variables.MakeReadonly(name);
        } else {
            variables.Export(name);
        }
    }
    return 0;
}

} // namespace

int BuiltinExport(Shell& shell, const std::vector<std::string>& args) {
    return BuiltinExportReadonly(shell, args, false);
}

int BuiltinReadonly(Shell& shell, const std::vector<std::string>& args) {
    return BuiltinExportReadonly(shell, args, true);
}

// unset [-fv] name ..., as dash's unsetcmd: -v (the default) removes
// variables, -f functions; the last option given wins. A bad name is "unset:
// <name>: bad variable name", a read-only variable "unset: <name>: is read
// only" -- both fatal (unset is special).
int BuiltinUnset(Shell& shell, const std::vector<std::string>& args) {
    bool functions = false;
    size_t i = 1;
    for (; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--") {
            ++i;
            break;
        }
        if (arg.size() < 2 || arg[0] != '-') {
            break;  // operands; "-" alone is one too
        }
        for (size_t j = 1; j < arg.size(); ++j) {
            if (arg[j] != 'v' && arg[j] != 'f') {
                shell.Fail(std::string("unset: Illegal option -") + arg[j]);
            }
            functions = arg[j] == 'f';
        }
    }
    for (; i < args.size(); ++i) {
        if (!IsValidShellName(args[i])) {
            shell.Fail("unset: " + args[i] + ": bad variable name");
        }
        if (functions) {
            shell.RemoveFunction(args[i]);  // unset -f: the function goes
            continue;
        }
        if (!shell.State().variables.Unset(args[i])) {
            shell.Fail("unset: " + args[i] + ": is read only");
        }
    }
    return 0;
}

// shift [n], as dash's shiftcmd: n defaults to 1 and must be a number of
// dash's atomax10 within an int -- else "shift: Illegal number: <n>" --
// and no more than there are positional parameters, else "shift: can't shift
// that many"; both fatal (shift is special). Further arguments are ignored.
int BuiltinShift(Shell& shell, const std::vector<std::string>& args) {
    size_t count = 1;
    if (args.size() > 1) {
        const std::optional<intmax_t> n = Atomax10(args[1]);
        if (!n || *n < 0 || *n > std::numeric_limits<int>::max()) {
            shell.Fail("shift: Illegal number: " + args[1]);
        }
        count = static_cast<size_t>(*n);
    }
    std::vector<std::string>& positional = shell.State().positional;
    if (count > positional.size()) {
        shell.Fail("shift: can't shift that many");
    }
    positional.erase(positional.begin(), positional.begin() + static_cast<ptrdiff_t>(count));
    return 0;
}

} // namespace Haisos::Hsh
