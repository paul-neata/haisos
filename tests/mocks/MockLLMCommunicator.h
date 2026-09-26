#pragma once
#include <string>
#include <functional>
#include <stdexcept>
#include "interfaces/ILLMCommunicator.h"
#include "interfaces/ILLMService.h"

namespace Haisos::Mocks {

class MockLLMCommunicator : public ILLMCommunicator {
public:
    MockLLMCommunicator()
        : m_callCount(0) {}

    void SetMessageResponse(const std::string& message) { m_messageResponse = message; }

    // Makes the next response ask for |toolName|, so an agent's tool-calling
    // path can be exercised without a real LLM.
    void SetToolCallResponse(const std::string& toolName) { m_toolCallName = toolName; }

    // Makes the next response carry exactly |toolCall|, however malformed, so
    // how an agent copes with whatever an LLM may send can be exercised.
    void SetRawToolCall(nlohmann::json toolCall) { m_rawToolCall = std::move(toolCall); }

    // Makes call number |callNumber| (counting from 1) throw |what| instead of
    // answering, the way a request that cannot be built or sent would.
    void SetThrowOnCall(int callNumber, const std::string& what) {
        m_throwOnCall = callNumber;
        m_throwWhat = what;
    }

    // Runs inside Call(), i.e. on the agent's own thread mid-round -- the only
    // point where something can happen "while the agent is busy".
    void SetOnCall(std::function<void()> onCall) { m_onCall = std::move(onCall); }

    LLMResponse Call(
        const std::vector<LLMMessage>& messages,
        const std::vector<std::tuple<std::string, std::string, nlohmann::json>>& /*availableTools*/) override
    {
        m_lastMessages = messages;
        ++m_callCount;

        if (m_onCall) {
            m_onCall();
        }
        if (m_callCount == m_throwOnCall) {
            throw std::runtime_error(m_throwWhat);
        }

        LLMResponse response;
        response.message.role = "assistant";
        response.message.content = m_messageResponse;
        response.done = true;

        if (!m_toolCallName.empty()) {
            nlohmann::json toolCall;
            toolCall["id"] = "call_1";
            toolCall["function"]["name"] = m_toolCallName;
            toolCall["function"]["arguments"] = nlohmann::json::object();
            response.message.toolCallsJson.push_back(std::move(toolCall));
            // One round of tool calls is enough for any test using this.
            m_toolCallName.clear();
        }
        if (!m_rawToolCall.is_null()) {
            response.message.toolCallsJson.push_back(std::move(m_rawToolCall));
            m_rawToolCall = nullptr;
        }

        return response;
    }

    const std::vector<LLMMessage>& GetLastMessages() const { return m_lastMessages; }
    int GetCallCount() const { return m_callCount; }

private:
    std::string m_messageResponse;
    std::string m_toolCallName;
    nlohmann::json m_rawToolCall;
    int m_throwOnCall = 0;
    std::string m_throwWhat;
    std::function<void()> m_onCall;
    std::vector<LLMMessage> m_lastMessages;
    int m_callCount;
};

}
