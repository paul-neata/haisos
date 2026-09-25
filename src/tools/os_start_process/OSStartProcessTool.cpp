#include "OSStartProcessTool.h"
#include "src/tools/os_tools_common/OSToolsCommon.h"
#include "src/components/Logger/Logger.h"

namespace Haisos::Tools {

const std::string OSStartProcessTool::ToolName = "os_start_process";
const std::string OSStartProcessTool::ToolDefaultDescription = "Start a new OS process (a .md agent, a .lua script, or a builtin command such as /bin/ls). A relative path is resolved against the calling process's working directory, and the new process starts in that same directory. Returns immediately with the new process's pid; does not wait for it to finish.";

OSStartProcessTool::OSStartProcessTool(std::shared_ptr<CurrentProcessHandle> process) : m_process(std::move(process)) {}

nlohmann::json OSStartProcessTool::GetDefaultParametersSchema() {
    return nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description", "Path to the .md or .lua program, or the builtin command, to run. A relative path is resolved against the process's working directory."}
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
    auto context = GetOSToolContext(m_process);
    if (!context.IsValid()) {
        return NoCurrentProcessError(ToolName);
    }

    // Resolved here rather than by the OS: IHaisosOS::StartProcess takes a
    // path against the OS root, and only this process's IFileIO knows where
    // "here" is for it.
    std::string path = context.io->ResolvePath(args["path"]);

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
    // what the OS was given, and its own edits stay its own. It starts in the
    // calling process's working directory, the way a shell would. It is never
    // interactive: the console's input belongs to whoever the haisosfile gave
    // it to, not to a process an agent happened to start.
    auto process = context.os->StartProcess(
        context.os->GetOsEnvironment()->Clone(), path, programArgs, context.io->GetCurrentDirectory(),
        StartProcessOptions{});
    if (!process) {
        LogWarning("OSStartProcessTool: failed to start process '%s'", path.c_str());
        return ToolResult{"Failed to start process: " + path, true};
    }

    nlohmann::json result;
    result["pid"] = process->GetPid();
    result["path"] = process->Path();
    return ToolResult{result.dump(), false};
}

}
