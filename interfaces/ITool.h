#pragma once
#include <memory>
#include <string>
#include <nlohmann/json.hpp>
#include "interfaces/IAgent.h"

namespace Haisos {

struct ToolResult {
    std::string content;
    bool isError = false;
};

class ITool {
public:
    virtual ~ITool() = default;
    virtual ToolResult Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& args) = 0;
    virtual nlohmann::json GetParametersSchema() const = 0;
};

}
