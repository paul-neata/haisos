#include "LLMCommunicator.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <atomic>

using json = nlohmann::json;

namespace Haisos {

LLMCommunicator::LLMCommunicator(
    std::unique_ptr<IHTTPClient> httpClient,
    const std::string& endpoint,
    const std::string& modelName,
    const std::string& apiKey)
    : m_httpClient(std::move(httpClient))
    , m_endpoint(endpoint)
    , m_modelName(modelName)
    , m_apiKey(apiKey)
{
}

LLMCommunicator::~LLMCommunicator() = default;

std::string LLMCommunicator::BuildRequestJson(
    const std::string& modelName,
    const std::vector<LLMMessage>& messages,
    std::vector<std::tuple<std::string, std::string, nlohmann::json>> tools)
{
    nlohmann::ordered_json request;
    request["model"] = modelName;

    if (!tools.empty()) {
        json toolsArray = json::array();
        for (auto& tool : tools) {
            json toolObj;
            toolObj["type"] = "function";
            toolObj["function"]["name"] = std::get<0>(tool);
            toolObj["function"]["description"] = std::get<1>(tool);
            toolObj["function"]["parameters"] = std::move(std::get<2>(tool));
            toolsArray.push_back(std::move(toolObj));
        }
        request["tools"] = std::move(toolsArray);
    }

    json messagesArray = json::array();

    for (const auto& msg : messages) {
        json message;
        message["role"] = msg.role;
        message["content"] = msg.content;
        if (!msg.thinking.empty()) {
            message["thinking"] = msg.thinking;
        }
        if (!msg.name.empty()) {
            message["name"] = msg.name;
        }
        if (msg.role == "tool" && !msg.tool_call_id.empty()) {
            message["tool_call_id"] = msg.tool_call_id;
        }
        if (!msg.toolCallsJson.empty()) {
            message["tool_calls"] = msg.toolCallsJson;
        }
        messagesArray.push_back(std::move(message));
    }

    request["messages"] = std::move(messagesArray);

    request["stream"] = false;

    return request.dump();
}

LLMResponse LLMCommunicator::ParseResponseJson(const std::string& jsonResponse, const SystemCallbacks& /*callbacks*/) {
    LLMResponse response;
    response.done = true;

    static std::atomic<int> s_toolCallIdCounter{0};

    try {
        json parsed = json::parse(jsonResponse);

        // Check for error response first
        if (parsed.contains("error")) {
            std::string errorMsg;
            const auto& errorNode = parsed["error"];
            if (errorNode.is_string()) {
                errorMsg = errorNode.get<std::string>();
            } else if (errorNode.is_object() && errorNode.contains("message") && errorNode["message"].is_string()) {
                errorMsg = errorNode["message"].get<std::string>();
            } else {
                errorMsg = errorNode.dump();
            }
            LogError("LLM API error: %s", errorMsg.c_str());
            response.message.role = "assistant";
            response.message.content = "Error: " + errorMsg;
            response.done = true;
            response.done_reason = "error";
            return response;
        }

        // Respect the role returned by the API, default to assistant
        if (parsed.contains("message") && parsed["message"].contains("role")) {
            response.message.role = parsed["message"]["role"];
        } else {
            response.message.role = "assistant";
        }

        // Try Ollama format: {"message": {"content": "..."}}
        if (parsed.contains("message") && parsed["message"].contains("content")) {
            const auto& contentNode = parsed["message"]["content"];
            if (contentNode.is_string()) {
                response.message.content = contentNode.get<std::string>();
            }
            if (parsed["message"].contains("thinking")) {
                response.message.thinking = parsed["message"]["thinking"];
            }
        }
        // Try direct format: {"content": "..."}
        else if (parsed.contains("content")) {
            const auto& contentNode = parsed["content"];
            if (contentNode.is_string()) {
                response.message.content = contentNode.get<std::string>();
            }
        }

        // Check for Ollama-format tool calls: message.tool_calls[].function.name / .arguments
        bool hasToolCalls = false;
        if (parsed.contains("message") && parsed["message"].contains("tool_calls")
            && parsed["message"]["tool_calls"].is_array()) {
            for (auto& tc : parsed["message"]["tool_calls"]) {
                if (!tc.contains("id") || !tc["id"].is_string() || tc["id"].get<std::string>().empty()) {
                    tc["id"] = "call_" + std::to_string(s_toolCallIdCounter.fetch_add(1));
                }
                response.message.toolCallsJson.push_back(std::move(tc));
                hasToolCalls = true;
            }
        }

        // Check for tool calls (Anthropic format) only if no Ollama-format tool calls were found
        if (!hasToolCalls && parsed.contains("stop_reason") && parsed["stop_reason"] == "tool_use") {
            if (parsed.contains("content") && parsed["content"].is_array()) {
                for (auto& item : parsed["content"]) {
                    if (item.contains("type") && item["type"] == "tool_use") {
                        json toolCall;
                        toolCall["type"] = "function";
                        toolCall["id"] = "call_" + std::to_string(s_toolCallIdCounter.fetch_add(1));
                        if (item.contains("name")) {
                            toolCall["function"]["name"] = item["name"];
                        }
                        if (item.contains("input")) {
                            toolCall["function"]["arguments"] = item["input"].dump();
                        } else {
                            toolCall["function"]["arguments"] = "{}";
                        }
                        response.message.toolCallsJson.push_back(std::move(toolCall));
                    }
                }
            }
        }

        // Set done and done_reason
        if (parsed.contains("done") && parsed["done"].is_boolean()) {
            response.done = parsed["done"];
        }
        if (parsed.contains("done_reason")) {
            response.done_reason = parsed["done_reason"];
        } else if (parsed.contains("stop_reason")) {
            response.done_reason = parsed["stop_reason"];
        }

    } catch (const json::exception& e) {
        LogError("Failed to parse LLM response: %s", e.what());
        response.message.role = "assistant";
        response.message.content = "Error: Failed to parse LLM response";
        response.done = true;
        response.done_reason = "parse_error";
    }

    return response;
}

LLMResponse LLMCommunicator::Call(
    const std::vector<LLMMessage>& messages,
    const std::vector<std::tuple<std::string, std::string, nlohmann::json>>& availableTools,
    const SystemCallbacks& callbacks)
{
    LogInfo("LLMCommunicator::Call - Endpoint: %s", m_endpoint.c_str());

    std::string requestJson = BuildRequestJson(m_modelName, messages, availableTools);

    LogVerboseDebug("[JSON_REQUEST] %s", requestJson.c_str());

    if (callbacks.on_send) {
        callbacks.on_send(requestJson);
    }

    std::vector<HTTPHeader> headers;
    headers.push_back({"Content-Type", "application/json"});
    if (!m_apiKey.empty()) {
        headers.push_back({"Authorization", "Bearer " + m_apiKey});
    }

    HTTPResponse httpResponse = m_httpClient->Post(m_endpoint, requestJson, headers);

    if (!httpResponse.IsSuccess()) {
        std::string bodyError;
        if (!httpResponse.body.empty()) {
            try {
                json bodyJson = json::parse(httpResponse.body);
                if (bodyJson.contains("error")) {
                    const auto& errorNode = bodyJson["error"];
                    if (errorNode.is_string()) {
                        bodyError = errorNode.get<std::string>();
                    } else if (errorNode.is_object() && errorNode.contains("message") && errorNode["message"].is_string()) {
                        bodyError = errorNode["message"].get<std::string>();
                    } else {
                        bodyError = errorNode.dump();
                    }
                } else {
                    bodyError = httpResponse.body;
                }
            } catch (...) {
                bodyError = httpResponse.body;
            }
        }
        LogError("HTTP request failed: status=%d error=%s body=%s", httpResponse.statusCode, httpResponse.error.c_str(), bodyError.c_str());
        LLMResponse response;
        response.done = true;
        response.done_reason = "http_error";
        response.message.role = "assistant";
        std::string errorDetail;
        if (!httpResponse.error.empty()) {
            errorDetail = httpResponse.error;
        } else {
            errorDetail = "status " + std::to_string(httpResponse.statusCode);
        }
        if (!bodyError.empty()) {
            response.message.content = "Error: HTTP request failed: " + errorDetail + " - " + bodyError;
        } else {
            response.message.content = "Error: HTTP request failed: " + errorDetail;
        }
        return response;
    }

    if (callbacks.on_received) {
        callbacks.on_received(httpResponse.body);
    }

    LogVerboseDebug("[JSON_RESPONSE] %s", httpResponse.body.c_str());

    return ParseResponseJson(httpResponse.body, callbacks);
}

}
