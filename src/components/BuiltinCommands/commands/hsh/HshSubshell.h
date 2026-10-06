#pragma once

#include <memory>
#include <string>
#include <vector>

#include "commands/hsh/HshShell.h"
#include "interfaces/IFileDescriptor.h"

namespace Haisos::Hsh {

// An in-process subshell: what a subshell may change and must not leak -- the
// ShellState (variables, options, positional parameters, $?, $!), the working
// directory, every slot of the descriptor table, and the job list -- saved at
// construction and put back at destruction (the directory first, then the
// slots). Also counts the shell's subshell depth. Never throws, never writes.
// hsh--control-flow adds the function table and the loop/function depths to
// what it saves.
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
};

} // namespace Haisos::Hsh
