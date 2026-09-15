#include "OSReadFileTool.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/Logger/Logger.h"

namespace Haisos::Tools {

const std::string OSReadFileTool::ToolName = "os_read_file";
const std::string OSReadFileTool::ToolDefaultDescription = "Read a text file from the OS's filesystem (rooted at the OS's mounted directory). Returns the file contents as a string, up to a 10 MB cap.";

OSReadFileTool::OSReadFileTool(IHaisosOS& os) : m_os(os) {}

nlohmann::json OSReadFileTool::GetDefaultParametersSchema() {
    return nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description", "Path to the file to read, relative to the OS's filesystem root"}
            }}
        }},
        {"required", nlohmann::json::array({"path"})}
    };
}

ToolResult OSReadFileTool::Call(std::shared_ptr<IAgent> /*callerAgent*/, const nlohmann::json& args) {
    if (!args.contains("path") || !args["path"].is_string()) {
        return ToolResult{"Missing required field: path", true};
    }
    std::string path = args["path"];
    LogDebug("OSReadFileTool: reading file '%s'", path.c_str());

    std::string content;
    if (!ReadWholeFile(*m_os.GetRootFileSystem(), path, content)) {
        LogWarning("OSReadFileTool: failed to read file '%s'", path.c_str());
        return ToolResult{"Failed to read file: " + path, true};
    }
    return ToolResult{content, false};
}

}
