#include "OSListProcessesTool.h"

namespace Haisos::Tools {

const std::string OSListProcessesTool::ToolName = "os_list_processes";
const std::string OSListProcessesTool::ToolDefaultDescription = "List the OS's currently running processes, with their pid, parent pid, the path they were started from, the name of the agent running them (empty when the process is not an agent), and whether they have finished.";

OSListProcessesTool::OSListProcessesTool(IHaisosOS& os) : m_os(os) {}

nlohmann::json OSListProcessesTool::GetDefaultParametersSchema() {
    return nlohmann::json{
        {"type", "object"},
        {"properties", nlohmann::json::object()},
        {"required", nlohmann::json::array()}
    };
}

ToolResult OSListProcessesTool::Call(std::shared_ptr<IAgent> /*callerAgent*/, const nlohmann::json& /*args*/) {
    nlohmann::json result = nlohmann::json::array();
    for (const auto& process : m_os.GetRunningProcesses()) {
        result.push_back({
            {"pid", process->GetPid()},
            {"parent_pid", process->GetParentPid()},
            {"path", process->Path()},
            {"agent_name", process->StartingAgentName()},
            // WaitToFinish(0) does not wait; it just reports whether the
            // process has finished.
            {"finished", process->WaitToFinish(0)}
        });
    }
    return ToolResult{result.dump(), false};
}

}
