#include "OSStartProcessTool.h"
#include "src/components/Logger/Logger.h"

namespace Haisos::Tools {

const std::string OSStartProcessTool::ToolName = "os_start_process";
const std::string OSStartProcessTool::ToolDefaultDescription = "Start a new OS process (a .md agent or a .lua script, resolved against the OS's filesystem root). Returns immediately with the new process's pid; does not wait for it to finish.";

OSStartProcessTool::OSStartProcessTool(IHaisosOS& os) : m_os(os) {}

nlohmann::json OSStartProcessTool::GetDefaultParametersSchema() {
    return nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description", "Path to the .md or .lua program to run, relative to the OS's filesystem root"}
            }},
            {"args", {
                {"type", "array"},
                {"items", {{"type", "string"}}},
                {"description", "Optional arguments passed to the process"}
            }}
        }},
        {"required", nlohmann::json::array({"path"})}
    };
}

ToolResult OSStartProcessTool::Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& args) {
    if (!args.contains("path") || !args["path"].is_string()) {
        return ToolResult{"Missing required field: path", true};
    }
    std::string path = args["path"];

    std::vector<std::string> programArgs;
    if (args.contains("args") && args["args"].is_array()) {
        for (const auto& a : args["args"]) {
            if (a.is_string()) {
                programArgs.push_back(a.get<std::string>());
            }
        }
    }

    LogDebug("OSStartProcessTool: starting process '%s' with %zu arg(s)", path.c_str(), programArgs.size());

    // The new process runs with a copy of the OS's environment: it inherits
    // what the OS was given, and its own edits stay its own.
    auto process = m_os.StartProcess(m_os.GetOsEnvironment()->Clone(), path, programArgs);
    if (!process) {
        LogWarning("OSStartProcessTool: failed to start process '%s'", path.c_str());
        return ToolResult{"Failed to start process: " + path, true};
    }

    nlohmann::json result;
    result["pid"] = process->GetPid();
    result["name"] = process->Name();
    return ToolResult{result.dump(), false};
}

}
