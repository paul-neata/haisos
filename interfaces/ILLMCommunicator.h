#pragma once
#include <string>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>
#include "JsonSendReceiveCallbacks.h"

namespace Haisos {

using json = nlohmann::json;

struct LLMMessage {
    std::string role;
    std::string content;
    std::string name;
    std::string thinking;
    std::string tool_call_id;
    std::vector<json> toolCallsJson;
};

struct LLMResponse {
    LLMMessage message;
    bool done = false;
    std::string done_reason;
};

class ILLMCommunicator {
public:
    virtual ~ILLMCommunicator() = default;

    virtual LLMResponse Call(
        const std::vector<LLMMessage>& messages,
        const std::vector<std::tuple<std::string, std::string, nlohmann::json>>& availableTools,
        const JsonSendReceiveCallbacks& callbacks) = 0;
};

}
