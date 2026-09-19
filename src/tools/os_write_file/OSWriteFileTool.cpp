#include "OSWriteFileTool.h"
#include "src/tools/os_tools_common/OSToolsCommon.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/Logger/Logger.h"

namespace Haisos::Tools {

const std::string OSWriteFileTool::ToolName = "os_write_file";
const std::string OSWriteFileTool::ToolDefaultDescription = "Write (creating or overwriting, unless append=true) a text file on the OS's filesystem. A relative path is resolved against the calling process's working directory.";

OSWriteFileTool::OSWriteFileTool(std::shared_ptr<CurrentProcessHandle> process) : m_process(std::move(process)) {}

nlohmann::json OSWriteFileTool::GetDefaultParametersSchema() {
    return nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description", "Path to the file to write. A relative path is resolved against the process's working directory."}
            }},
            {"content", {
                {"type", "string"},
                {"description", "The content to write to the file"}
            }},
            {"append", {
                {"type", "boolean"},
                {"description", "If true, append to the file instead of overwriting it. Defaults to false."}
            }}
        }},
        {"required", nlohmann::json::array({"path", "content"})}
    };
}

ToolResult OSWriteFileTool::Call(std::shared_ptr<IAgent> /*callerAgent*/, const nlohmann::json& args) {
    if (!args.contains("path") || !args["path"].is_string()) {
        return ToolResult{"Missing required field: path", true};
    }
    if (!args.contains("content") || !args["content"].is_string()) {
        return ToolResult{"Missing required field: content", true};
    }
    auto context = GetOSToolContext(m_process);
    if (!context.IsValid()) {
        return NoCurrentProcessError(ToolName);
    }

    std::string path = context.ResolvePath(args["path"]);
    std::string content = args["content"];
    bool append = args.value("append", false);

    LogDebug("OSWriteFileTool: writing file '%s' append=%d", path.c_str(), append ? 1 : 0);

    auto fs = context.os->GetRootFileSystem();
    int fd = fs->OpenFile(path, append ? kFileOpenWriteCreateAppend : kFileOpenWriteCreateTruncate, kFileCreateMode);
    if (fd < 0) {
        LogWarning("OSWriteFileTool: failed to open file '%s' for writing (fd=%d)", path.c_str(), fd);
        return ToolResult{"Failed to open file for writing: " + path, true};
    }

    ssize_t written = fs->WriteFile(fd, content.data(), content.size());
    fs->CloseFile(fd);

    if (written < 0 || static_cast<size_t>(written) != content.size()) {
        LogWarning("OSWriteFileTool: failed to write file '%s' (written=%zd, expected=%zu)", path.c_str(), written, content.size());
        return ToolResult{"Failed to write file: " + path, true};
    }
    return ToolResult{"OK", false};
}

}
