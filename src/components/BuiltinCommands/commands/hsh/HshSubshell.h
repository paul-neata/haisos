#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "commands/hsh/HshAst.h"
#include "commands/hsh/HshShell.h"
#include "interfaces/IFileDescriptor.h"

namespace Haisos::Hsh {

// An in-process subshell: what a subshell may change and must not leak -- the
// ShellState (variables, options, positional parameters, $?, $!), the working
// directory, every slot of the descriptor table, the job list, the function
// table and the loop/function/dot depths and the errexit suppression -- saved
// at construction and put back at destruction (the directory first, then the
// slots). Jobs the subshell added are dropped with its job list: their
// processes leave the shell's live children there and then, or repeated
// `( /bin/true & )` would keep them for the life of the shell. Also counts
// the shell's subshell depth. Never throws, never writes.
class SubshellScope {
public:
    explicit SubshellScope(Shell& shell);
    ~SubshellScope();
    SubshellScope(const SubshellScope&) = delete;
    SubshellScope& operator=(const SubshellScope&) = delete;

private:
    Shell& m_shell;
    ShellState m_state;
    std::string m_workingDirectory;
    std::vector<std::shared_ptr<IFileDescriptor>> m_slots;
    std::vector<Shell::Job> m_jobs;
    std::map<std::string, CommandPtr> m_functions;
    int m_loopDepth;
    int m_functionDepth;
    int m_dotDepth;
    int m_errexitSuppressed;
};

} // namespace Haisos::Hsh
