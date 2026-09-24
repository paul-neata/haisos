#include "OSReadFileTool.h"
#include "src/tools/os_tools_common/OSToolsCommon.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/Logger/Logger.h"

namespace Haisos::Tools {

const std::string OSReadFileTool::ToolName = "os_read_file";
const std::string OSReadFileTool::ToolDefaultDescription = "Read a text file from the OS's filesystem. A relative path is resolved against the calling process's working directory. Returns the file contents as a string, up to a 10 MB cap.";

OSReadFileTool::OSReadFileTool(std::shared_ptr<CurrentProcessHandle> process) : m_process(std::move(process)) {}

nlohmann::json OSReadFileTool::GetDefaultParametersSchema() {
    return nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description", "Path to the file to read. A relative path is resolved against the process's working directory."}
            }}
        }},
        {"required", nlohmann::json::array({"path"})}
    };
}

ToolResult OSReadFileTool::Call(std::shared_ptr<IAgent> /*callerAgent*/, const nlohmann::json& args) {
    if (!args.contains("path") || !args["path"].is_string()) {
        return ToolResult{"Missing required field: path", true};
    }
    auto context = GetOSToolContext(m_process);
    if (!context.IsValid()) {
        return NoCurrentProcessError(ToolName);
    }

    std::string path = args["path"];
    LogDebug("OSReadFileTool: reading file '%s' (resolved to '%s')",
        path.c_str(), context.io->ResolvePath(path).c_str());

    std::string content;
    if (!ReadWholeFile(*context.io, path, content)) {
        LogWarning("OSReadFileTool: failed to read file '%s'", path.c_str());
        return ToolResult{"Failed to read file: " + path, true};
    }
    return ToolResult{content, false};
}

}
