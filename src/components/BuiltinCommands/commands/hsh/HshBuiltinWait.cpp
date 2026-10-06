#include "commands/hsh/HshBuiltins.h"

#include <algorithm>
#include <cstdlib>

#include "commands/hsh/HshShell.h"
#include "interfaces/IProcess.h"

namespace Haisos::Hsh {

// wait [pid ...], as dash: with no operand, wait for every job (status always
// 0); with pids, wait for each one's job and answer with its status (127 when
// it is no job, with no message). A `%...` job spec names a job of the job
// control hsh does not have. Interruptible: a stop of the shell unwinds out
// of WaitForChild (ShellStopped).
int BuiltinWait(Shell& shell, const std::vector<std::string>& args) {
    if (args.size() == 1) {
        for (Shell::Job& job : shell.Jobs()) {
            for (const std::shared_ptr<IProcess>& process : job.processes) {
                shell.WaitForChild(process);
            }
        }
        shell.Jobs().clear();
        return 0;
    }
    int status = 0;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& operand = args[i];
        if (!operand.empty() && operand[0] == '%') {
            shell.Report("wait: No such job: " + operand);
            status = 2;
            continue;
        }
        const bool digits = !operand.empty() && std::all_of(operand.begin(), operand.end(),
            [](char c) { return c >= '0' && c <= '9'; });
        if (!digits) {
            shell.Report("wait: Illegal number: " + operand);
            status = 2;
            continue;
        }
        const uint64_t pid = std::strtoull(operand.c_str(), nullptr, 10);
        auto& jobs = shell.Jobs();
        const auto found = std::find_if(jobs.begin(), jobs.end(), [&](const Shell::Job& job) {
            if (job.pid == pid) {
                return true;
            }
            return std::any_of(job.processes.begin(), job.processes.end(),
                [&](const std::shared_ptr<IProcess>& process) { return process->GetPid() == pid; });
        });
        if (found == jobs.end()) {
            status = 127;  // no such process: no message, as dash
            continue;
        }
        for (const std::shared_ptr<IProcess>& process : found->processes) {
            status = shell.WaitForChild(process);
        }
        jobs.erase(found);
    }
    return status;
}

} // namespace Haisos::Hsh
