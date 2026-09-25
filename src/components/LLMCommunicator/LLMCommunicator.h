#pragma once
#include <string>
#include <memory>
#include "interfaces/ILLMCommunicator.h"
#include "interfaces/INetworkService.h"
#include "src/components/Logger/Logger.h"

namespace Haisos {

class LLMCommunicator : public ILLMCommunicator {
public:
    // sourceName, when non-empty, tags this communicator's [JSON_REQUEST]/
    // [JSON_RESPONSE] trace log lines with it (e.g. an agent's name), so
    // concurrent agents' traffic can still be told apart in the logs. It is
    // also the agent name every request and response is reported under via
    // LogAgentSend/LogAgentReceive (see Logger.h).
    static std::shared_ptr<LLMCommunicator> Create(
        std::shared_ptr<IHTTPClient> httpClient,
        const std::string& endpoint,
        const std::string& modelName,
        const std::string& apiKey,
        const std::string& sourceName = "");

    ~LLMCommunicator() override;

    LLMResponse Call(
        const std::vector<LLMMessage>& messages,
        const std::vector<std::tuple<std::string, std::string, nlohmann::json>>& availableTools) override;

    static std::string BuildRequestJson(
        const std::string& modelName,
        const std::vector<LLMMessage>& messages,
        const std::vector<std::tuple<std::string, std::string, nlohmann::json>>& tools);

    IHTTPClient* GetHttpClient() const { return m_httpClient.get(); }

private:
    LLMCommunicator(
        std::shared_ptr<IHTTPClient> httpClient,
        const std::string& endpoint,
        const std::string& modelName,
        const std::string& apiKey,
        const std::string& sourceName);

    LLMResponse ParseResponseJson(const std::string& jsonResponse);

    std::shared_ptr<IHTTPClient> m_httpClient;
    std::string m_endpoint;
    std::string m_modelName;
    std::string m_apiKey;
    std::string m_sourceName;
};

}
