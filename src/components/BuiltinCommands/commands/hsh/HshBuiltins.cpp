#include "commands/hsh/HshBuiltins.h"

#include <algorithm>
#include <climits>

#include "commands/hsh/HshShell.h"

namespace Haisos::Hsh {

int BuiltinColon(Shell&, const std::vector<std::string>&) {
    return 0;
}

int BuiltinExit(Shell& shell, const std::vector<std::string>& args) {
    int status = shell.State().lastExitStatus;
    if (args.size() > 1) {
        const std::string& text = args[1];
        // One or more decimal digits fitting an int; anything else is dash's
        // "exit: Illegal number: <n>" (exit -1, exit abc), a fatal error.
        int value = 0;
        bool valid = !text.empty();
        for (const char c : text) {
            const int digit = c - '0';
            if (c < '0' || c > '9' || value > (INT_MAX - digit) / 10) {
                valid = false;
                break;
            }
            value = value * 10 + digit;
        }
        if (!valid) {
            shell.Fail("exit: Illegal number: " + text);
        }
        status = value;
        // Further arguments are ignored, as dash ignores them.
    }
    throw ShellExit{status & 0xFF};
}

int BuiltinFalse(Shell&, const std::vector<std::string>&) {
    return 1;
}

int BuiltinTrue(Shell&, const std::vector<std::string>&) {
    return 0;
}

const std::vector<ShellBuiltin>& ShellBuiltins() {
    static const std::vector<ShellBuiltin> builtins = {
        {":", true, &BuiltinColon},
        {"exec", true, &BuiltinExec},
        {"exit", true, &BuiltinExit},
        {"false", false, &BuiltinFalse},
        {"true", false, &BuiltinTrue},
        {"wait", false, &BuiltinWait},
    };
    return builtins;
}

const ShellBuiltin* FindShellBuiltin(const std::string& name) {
    const auto& builtins = ShellBuiltins();
    const auto it = std::lower_bound(builtins.begin(), builtins.end(), name,
        [](const ShellBuiltin& builtin, const std::string& sought) { return builtin.name < sought; });
    return it != builtins.end() && name == it->name ? &*it : nullptr;
}

} // namespace Haisos::Hsh
