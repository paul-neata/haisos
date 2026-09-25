#pragma once
#include <string>
#include <memory>
#include <vector>
#include "interfaces/ILLMCommunicator.h"
#include "interfaces/INetworkService.h"
#include "src/components/Logger/Logger.h"

namespace Haisos {

class LLMCommunicator : public ILLMCommunicator {
public:
    // agentPath, when non-empty, is the path of the agent this communicator
    // talks for (its ancestors' names, then its own; see Logger.h). It tags
    // this communicator's [JSON_REQUEST]/[JSON_RESPONSE] trace log lines (as
    // "a>b>c"), so concurrent agents' traffic can still be told apart in the
    // logs, and it is what every request and response is reported under via
    // LogAgentSend/LogAgentReceive.
    static std::shared_ptr<LLMCommunicator> Create(
        std::shared_ptr<IHTTPClient> httpClient,
        const std::string& endpoint,
        const std::string& modelName,
        const std::string& apiKey,
        std::vector<std::string> agentPath = {});

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
        std::vector<std::string> agentPath);

    LLMResponse ParseResponseJson(const std::string& jsonResponse);

    std::shared_ptr<IHTTPClient> m_httpClient;
    std::string m_endpoint;
    std::string m_modelName;
    std::string m_apiKey;
    std::vector<std::string> m_agentPath;
    // m_agentPath as text, for the trace log lines; empty if the path is.
    std::string m_sourceName;
};

}
