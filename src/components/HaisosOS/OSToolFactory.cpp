#include "OSToolFactory.h"
#include <algorithm>
#include "src/components/Logger/Logger.h"
#include "src/tools/os_read_file/OSReadFileTool.h"
#include "src/tools/os_write_file/OSWriteFileTool.h"
#include "src/tools/os_list_directory/OSListDirectoryTool.h"
#include "src/tools/os_start_process/OSStartProcessTool.h"
#include "src/tools/os_list_processes/OSListProcessesTool.h"

namespace Haisos {

OSToolFactory::OSToolFactory(IHaisosOS& os) : m_os(os) {
    m_registry = {
        ToolEntry{
            Tools::OSReadFileTool::ToolName,
            []() { return Tools::OSReadFileTool::ToolDefaultDescription; },
            []() { return Tools::OSReadFileTool::GetDefaultParametersSchema(); },
            [this]() -> std::shared_ptr<ITool> { return Tools::OSReadFileTool::Create(m_os); }
        },
        ToolEntry{
            Tools::OSWriteFileTool::ToolName,
            []() { return Tools::OSWriteFileTool::ToolDefaultDescription; },
            []() { return Tools::OSWriteFileTool::GetDefaultParametersSchema(); },
            [this]() -> std::shared_ptr<ITool> { return Tools::OSWriteFileTool::Create(m_os); }
        },
        ToolEntry{
            Tools::OSListDirectoryTool::ToolName,
            []() { return Tools::OSListDirectoryTool::ToolDefaultDescription; },
            []() { return Tools::OSListDirectoryTool::GetDefaultParametersSchema(); },
            [this]() -> std::shared_ptr<ITool> { return Tools::OSListDirectoryTool::Create(m_os); }
        },
        ToolEntry{
            Tools::OSListProcessesTool::ToolName,
            []() { return Tools::OSListProcessesTool::ToolDefaultDescription; },
            []() { return Tools::OSListProcessesTool::GetDefaultParametersSchema(); },
            [this]() -> std::shared_ptr<ITool> { return Tools::OSListProcessesTool::Create(m_os); }
        },
        ToolEntry{
            Tools::OSStartProcessTool::ToolName,
            []() { return Tools::OSStartProcessTool::ToolDefaultDescription; },
            []() { return Tools::OSStartProcessTool::GetDefaultParametersSchema(); },
            [this]() -> std::shared_ptr<ITool> { return Tools::OSStartProcessTool::Create(m_os); }
        },
    };
}

std::shared_ptr<ITool> OSToolFactory::CreateTool(const std::string& name, std::shared_ptr<IAgent> /*callerAgent*/) {
    for (const auto& entry : m_registry) {
        if (entry.name == name) {
            return entry.create();
        }
    }
    LogWarning("OSToolFactory: Unknown tool requested: %s", name.c_str());
    return nullptr;
}

bool OSToolFactory::HasTool(const std::string& name) const {
    return std::find_if(m_registry.begin(), m_registry.end(),
                        [&name](const ToolEntry& entry) { return entry.name == name; }) != m_registry.end();
}

std::vector<std::string> OSToolFactory::GetAvailableTools() const {
    std::vector<std::string> toolNames;
    toolNames.reserve(m_registry.size());
    for (const auto& entry : m_registry) {
        toolNames.push_back(entry.name);
    }
    return toolNames;
}

std::vector<std::tuple<std::string, std::string, nlohmann::json>> OSToolFactory::GetAvailableToolDescriptions() const {
    std::vector<std::tuple<std::string, std::string, nlohmann::json>> toolDescriptions;
    toolDescriptions.reserve(m_registry.size());
    for (const auto& entry : m_registry) {
        toolDescriptions.emplace_back(entry.name, entry.getDescription(), entry.getSchema());
    }
    return toolDescriptions;
}

}
