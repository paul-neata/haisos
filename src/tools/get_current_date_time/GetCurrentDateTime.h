#pragma once
#include <string>
#include <memory>
#include "interfaces/ILLMService.h"

namespace Haisos::Tools {

class GetCurrentDateTime : public ITool {
public:
    static const std::string ToolName;
    static const std::string ToolDefaultDescription;

    static std::shared_ptr<GetCurrentDateTime> Create() {
        return std::shared_ptr<GetCurrentDateTime>(new GetCurrentDateTime());
    }

    ToolResult Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& args) override;
    static nlohmann::json GetDefaultParametersSchema();
    nlohmann::json GetParametersSchema() const override { return GetDefaultParametersSchema(); }

private:
    GetCurrentDateTime() = default;
};

}
