#include "OSWriteFileTool.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos::Tools {

const std::string OSWriteFileTool::ToolName = "os_write_file";
const std::string OSWriteFileTool::ToolDefaultDescription = "Write (creating or overwriting, unless append=true) a text file on the OS's filesystem (rooted at the OS's mounted directory).";

OSWriteFileTool::OSWriteFileTool(IHaisosOS& os) : m_os(os) {}

nlohmann::json OSWriteFileTool::GetDefaultParametersSchema() {
    return nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description", "Path to the file to write, relative to the OS's filesystem root"}
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
    std::string path = args["path"];
    std::string content = args["content"];
    bool append = args.value("append", false);

    auto& fs = m_os.GetFileSystem();
    int fd = fs.OpenFile(path, append ? kFileOpenWriteCreateAppend : kFileOpenWriteCreateTruncate, kFileCreateMode);
    if (fd < 0) {
        return ToolResult{"Failed to open file for writing: " + path, true};
    }

    ssize_t written = fs.WriteFile(fd, content.data(), content.size());
    fs.CloseFile(fd);

    if (written < 0 || static_cast<size_t>(written) != content.size()) {
        return ToolResult{"Failed to write file: " + path, true};
    }
    return ToolResult{"OK", false};
}

}
