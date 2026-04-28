#pragma once
#include <string>
#include "interfaces/ITool.h"

namespace Haisos::Tools {

class GetCurrentDateTime : public ITool {
public:
    static const std::string ToolName;
    static const std::string ToolDefaultDescription;

    std::string Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& args) override;
    static nlohmann::json GetDefaultParametersSchema();
    nlohmann::json GetParametersSchema() const override { return GetDefaultParametersSchema(); }
};

}
