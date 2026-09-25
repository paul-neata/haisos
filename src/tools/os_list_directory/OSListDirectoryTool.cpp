#include "OSListDirectoryTool.h"
#include "src/tools/os_tools_common/OSToolsCommon.h"
#include "src/components/Logger/Logger.h"

namespace Haisos::Tools {

const std::string OSListDirectoryTool::ToolName = "os_list_directory";
const std::string OSListDirectoryTool::ToolDefaultDescription = "List the entries (files and directories) of a directory on the OS's filesystem. A relative path is resolved against the calling process's working directory, which is also the default.";

OSListDirectoryTool::OSListDirectoryTool(std::shared_ptr<CurrentProcessHandle> process) : m_process(std::move(process)) {}

nlohmann::json OSListDirectoryTool::GetDefaultParametersSchema() {
    return nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description", "Path to the directory to list. A relative path is resolved against the process's working directory. Defaults to \".\" (the working directory itself)."}
            }}
        }},
        {"required", nlohmann::json::array()}
    };
}

ToolResult OSListDirectoryTool::Call(std::shared_ptr<IAgent> /*callerAgent*/, const nlohmann::json& args) {
    auto context = GetOSToolContext(m_process);
    if (!context.IsValid()) {
        return NoCurrentProcessError(ToolName);
    }

    std::string path = args.value("path", ".");
    LogDebug("OSListDirectoryTool: listing directory '%s' (resolved to '%s')",
        path.c_str(), context.io->ResolvePath(path).c_str());

    auto entries = context.io->ReadDirectory(path);

    nlohmann::json result = nlohmann::json::array();
    for (const auto& entry : entries) {
        // Every directory has them; listing them tells the caller nothing.
        if (entry.name == "." || entry.name == "..") {
            continue;
        }
        const char* type = entry.type == DirectoryEntryType::Dir ? "dir"
            : entry.type == DirectoryEntryType::CharDevice ? "char_device"
            : "file";
        result.push_back({
            {"name", entry.name},
            {"type", type}
        });
    }
    // A filename is an arbitrary byte string, so it need not be valid UTF-8;
    // the default dump() would throw on one, which is not a failure this tool
    // should turn into an exception escaping into the caller.
    return ToolResult{result.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace), false};
}

}
