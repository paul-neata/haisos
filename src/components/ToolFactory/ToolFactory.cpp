#include "ToolFactory.h"
#include "src/tools/get_current_date_time/GetCurrentDateTime.h"
#include "src/tools/agent_start/AgentStartTool.h"
#include "src/tools/agent_wait_to_finish/AgentWaitToFinishTool.h"
#include "src/tools/agent_query/AgentQueryTool.h"
#include "src/tools/agent_stop/AgentStopTool.h"
#include "src/tools/agent_list_running/AgentListRunningTool.h"

namespace Haisos {

ToolFactory::ToolFactory() : m_factory(nullptr) {
    m_registry = {
        ToolEntry{
            Tools::GetCurrentDateTime::ToolName,
            []() { return Tools::GetCurrentDateTime::ToolDefaultDescription; },
            []() { return Tools::GetCurrentDateTime::GetDefaultParametersSchema(); },
            [](std::shared_ptr<IAgent>) { return std::make_unique<Tools::GetCurrentDateTime>(); }
        },
        ToolEntry{
            Tools::AgentStartTool::ToolName,
            []() { return Tools::AgentStartTool::ToolDefaultDescription; },
            []() { return Tools::AgentStartTool::GetDefaultParametersSchema(); },
            [this](std::shared_ptr<IAgent> callerAgent) -> std::unique_ptr<ITool> {
                if (m_factory && callerAgent) {
                    return std::make_unique<Tools::AgentStartTool>(*m_factory);
                }
                LogWarning("ToolFactory: agent_start requested but factory or caller agent is missing");
                return nullptr;
            }
        },
        ToolEntry{
            Tools::AgentWaitToFinishTool::ToolName,
            []() { return Tools::AgentWaitToFinishTool::ToolDefaultDescription; },
            []() { return Tools::AgentWaitToFinishTool::GetDefaultParametersSchema(); },
            [](std::shared_ptr<IAgent> callerAgent) -> std::unique_ptr<ITool> {
                if (callerAgent) {
                    return std::make_unique<Tools::AgentWaitToFinishTool>();
                }
                LogWarning("ToolFactory: agent_wait_to_finish requested but caller agent is missing");
                return nullptr;
            }
        },
        ToolEntry{
            Tools::AgentQueryTool::ToolName,
            []() { return Tools::AgentQueryTool::ToolDefaultDescription; },
            []() { return Tools::AgentQueryTool::GetDefaultParametersSchema(); },
            [](std::shared_ptr<IAgent> callerAgent) -> std::unique_ptr<ITool> {
                if (callerAgent) {
                    return std::make_unique<Tools::AgentQueryTool>();
                }
                LogWarning("ToolFactory: agent_query requested but caller agent is missing");
                return nullptr;
            }
        },
        ToolEntry{
            Tools::AgentStopTool::ToolName,
            []() { return Tools::AgentStopTool::ToolDefaultDescription; },
            []() { return Tools::AgentStopTool::GetDefaultParametersSchema(); },
            [](std::shared_ptr<IAgent> callerAgent) -> std::unique_ptr<ITool> {
                if (callerAgent) {
                    return std::make_unique<Tools::AgentStopTool>();
                }
                LogWarning("ToolFactory: agent_stop requested but caller agent is missing");
                return nullptr;
            }
        },
        ToolEntry{
            Tools::AgentListRunningTool::ToolName,
            []() { return Tools::AgentListRunningTool::ToolDefaultDescription; },
            []() { return Tools::AgentListRunningTool::GetDefaultParametersSchema(); },
            [](std::shared_ptr<IAgent> callerAgent) -> std::unique_ptr<ITool> {
                if (callerAgent) {
                    return std::make_unique<Tools::AgentListRunningTool>();
                }
                LogWarning("ToolFactory: agent_list_running requested but caller agent is missing");
                return nullptr;
            }
        },
    };
}

ToolFactory::ToolFactory(IFactory& factory) : ToolFactory() {
    m_factory = &factory;
}

std::unique_ptr<ITool> ToolFactory::CreateTool(const std::string& name, std::shared_ptr<IAgent> callerAgent) {
    for (const auto& entry : m_registry) {
        if (entry.name == name) {
            return entry.create(callerAgent);
        }
    }
    LogWarning("ToolFactory: Unknown tool requested: %s", name.c_str());
    return nullptr;
}

std::vector<std::string> ToolFactory::GetAvailableTools() const {
    std::vector<std::string> toolNames;
    toolNames.reserve(m_registry.size());
    for (const auto& entry : m_registry) {
        toolNames.push_back(entry.name);
    }
    return toolNames;
}

std::vector<std::tuple<std::string, std::string, nlohmann::json>> ToolFactory::GetAvailableToolDescriptions() const {
    std::vector<std::tuple<std::string, std::string, nlohmann::json>> toolDescriptions;
    toolDescriptions.reserve(m_registry.size());
    for (const auto& entry : m_registry) {
        toolDescriptions.emplace_back(entry.name, entry.getDescription(), entry.getSchema());
    }
    return toolDescriptions;
}

} // namespace Haisos
