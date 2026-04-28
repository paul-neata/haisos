#pragma once
#include <memory>
#include <string>
#include <vector>
#include "ITool.h"

namespace Haisos {

class IAgent;

class IToolFactory {
public:
    virtual ~IToolFactory() = default;

    virtual std::unique_ptr<ITool> CreateTool(const std::string& name, std::shared_ptr<IAgent> callerAgent = nullptr) = 0;
    virtual std::vector<std::string> GetAvailableTools() const = 0;
    virtual std::vector<std::tuple<std::string, std::string, nlohmann::json>> GetAvailableToolDescriptions() const = 0;
};

}
