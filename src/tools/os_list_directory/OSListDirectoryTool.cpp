#include "OSListDirectoryTool.h"

namespace Haisos::Tools {

const std::string OSListDirectoryTool::ToolName = "os_list_directory";
const std::string OSListDirectoryTool::ToolDefaultDescription = "List the entries (files and directories) of a directory on the OS's filesystem (rooted at the OS's mounted directory).";

OSListDirectoryTool::OSListDirectoryTool(IHaisosOS& os) : m_os(os) {}

nlohmann::json OSListDirectoryTool::GetDefaultParametersSchema() {
    return nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description", "Path to the directory to list, relative to the OS's filesystem root. Defaults to \".\" (the root)."}
            }}
        }},
        {"required", nlohmann::json::array()}
    };
}

ToolResult OSListDirectoryTool::Call(std::shared_ptr<IAgent> /*callerAgent*/, const nlohmann::json& args) {
    std::string path = args.value("path", ".");

    auto entries = m_os.GetFileSystemService().GetFileSystem().ReadDirectory(path);

    nlohmann::json result = nlohmann::json::array();
    for (const auto& entry : entries) {
        result.push_back({
            {"name", entry.name},
            {"type", entry.type == DirectoryEntryType::Dir ? "dir" : "file"}
        });
    }
    return ToolResult{result.dump(), false};
}

}
