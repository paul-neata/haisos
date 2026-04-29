#pragma once

#include <iostream>
#include <mutex>
#include <string>
#include <cstdlib>
#include <tuple>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <functional>
#include "src/components/HTTPClient/HTTPClient.h"
#include <nlohmann/json.hpp>

namespace Haisos::IntegrationTest {

inline std::mutex& GetConsoleMutex() {
    static std::mutex mutex;
    return mutex;
}

class ConsoleLock {
public:
    ConsoleLock() : lock(GetConsoleMutex()) {}
private:
    std::lock_guard<std::mutex> lock;
};

inline std::string GetEnvOrDefault(const char* name, const std::string& defaultValue) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : defaultValue;
}

inline std::string GetFirstAvailableModel(const std::string& endpoint) {
    try {
        auto client = CreateHTTPClient();
        std::string tagsUrl = endpoint;
        size_t pos = tagsUrl.find("/api/chat");
        if (pos != std::string::npos) {
            tagsUrl = tagsUrl.substr(0, pos) + "/api/tags";
        }
        HTTPResponse response = client->Get(tagsUrl);
        if (response.IsSuccess() && !response.body.empty()) {
            nlohmann::json j = nlohmann::json::parse(response.body, nullptr, false);
            if (j.contains("models") && j["models"].is_array() && !j["models"].empty()) {
                return j["models"][0].value("name", "llama3");
            }
        }
    } catch (...) {
        // Fall through to default
    }
    return "llama3";
}

inline std::tuple<std::string, std::string, std::string> GetEndpointModelAndApiKey() {
    std::string endpoint = GetEnvOrDefault("HAISOS_ENDPOINT", "http://localhost:11434/api/chat");
    std::string model    = GetEnvOrDefault("HAISOS_MODEL",    "llama3");
    std::string apiKey   = GetEnvOrDefault("HAISOS_API_KEY",  "");

    if (model == "llama3") {
        std::string available = GetFirstAvailableModel(endpoint);
        if (!available.empty()) {
            model = available;
        }
    }

    return {endpoint, model, apiKey};
}

inline std::string GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t);
#else
    localtime_r(&time_t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

inline std::string PrettyPrintJson(const std::string& jsonStr) {
    try {
        auto j = nlohmann::json::parse(jsonStr, nullptr, false);
        return j.dump(2);
    } catch (...) {
        return jsonStr;
    }
}

inline std::function<void(const std::string&)> MakeLLMJsonLogger(const std::string& direction, const std::string& agentName = "") {
    return [direction, agentName](const std::string& json) {
        ConsoleLock lock;
        std::cout << "---- " << direction << " ---- " << GetCurrentTimestamp();
        if (!agentName.empty()) {
            std::cout << " @ " << agentName;
        }
        std::cout << "\n";
        std::cout << PrettyPrintJson(json) << "\n\n" << std::flush;
    };
}

inline void PrintTestStart(const std::string& testName, bool printEnvVars = true) {
    ConsoleLock lock;
    std::cout << "\n\n";
    std::cout << "================= " << testName << " ===================\n";
    if (printEnvVars) {
        std::cout << "HAISOS_ENDPOINT=" << GetEnvOrDefault("HAISOS_ENDPOINT", "http://localhost:11434/api/chat") << "\n";
        std::cout << "HAISOS_MODEL=" << GetEnvOrDefault("HAISOS_MODEL", "llama3") << "\n";
    }
    std::cout << std::flush;
}

inline void PrintTestEnd(const std::string& testName, bool success) {
    ConsoleLock lock;
    std::cout << "================= " << testName << " ";
    if (success) {
        std::cout << "SUCCEEDED";
    } else {
        std::cout << "FAILED";
    }
    std::cout << " ===================\n\n";
    std::cout << std::flush;
}

}
