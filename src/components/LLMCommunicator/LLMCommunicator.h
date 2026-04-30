#pragma once
#include <string>
#include <memory>
#include "interfaces/ILLMCommunicator.h"
#include "interfaces/IHTTPClient.h"
#include "src/components/Logger/Logger.h"

namespace Haisos {

class LLMCommunicator : public ILLMCommunicator {
public:
    LLMCommunicator(
        std::unique_ptr<IHTTPClient> httpClient,
        const std::string& endpoint,
        const std::string& modelName,
        const std::string& apiKey);

    ~LLMCommunicator() override;

    LLMResponse Call(
        const std::vector<LLMMessage>& messages,
        const std::vector<std::tuple<std::string, std::string, nlohmann::json>>& availableTools,
        const SystemCallbacks& callbacks) override;

    static std::string BuildRequestJson(
        const std::string& modelName,
        const std::vector<LLMMessage>& messages,
        std::vector<std::tuple<std::string, std::string, nlohmann::json>> tools);

    IHTTPClient* GetHttpClient() const { return m_httpClient.get(); }

private:
    LLMResponse ParseResponseJson(const std::string& jsonResponse, const SystemCallbacks& callbacks);

    std::unique_ptr<IHTTPClient> m_httpClient;
    std::string m_endpoint;
    std::string m_modelName;
    std::string m_apiKey;
};

}
