#include "commands/hsh/HshBuiltins.h"

#include "commands/hsh/HshShell.h"
#include "interfaces/IProcess.h"

namespace Haisos::Hsh {

// exec [--] [name args...], as dash: with no command the redirections of its
// command stay (the executor's RedirectionScope keeps them); with one, the
// command runs in the shell's place and the shell ends with its status.
int BuiltinExec(Shell& shell, const std::vector<std::string>& args) {
    size_t i = 1;
    if (i < args.size() && args[i] == "--") {
        ++i;
    }
    if (i == args.size()) {
        shell.KeepRedirections();
        return 0;
    }
    const std::string& name = args[i];
    // PATH and paths only, never a shell builtin (dash: `exec :` is
    // "exec: :: not found").
    const Shell::CommandLookup lookup = shell.LookUpCommand(name);
    if (lookup.result == Shell::CommandLookup::Result::NotFound) {
        shell.Report("exec: " + name + ": not found");
        throw ShellExit{127};
    }
    std::shared_ptr<IProcess> child;
    if (lookup.result == Shell::CommandLookup::Result::Found) {
        child = shell.StartChild(lookup.path,
            std::vector<std::string>(args.begin() + static_cast<ptrdiff_t>(i) + 1, args.end()), {});
    }
    if (!child) {
        shell.Report("exec: " + name + ": Permission denied");
        throw ShellExit{126};
    }
    throw ShellExit{shell.WaitForChild(child)};
}

} // namespace Haisos::Hsh
