#include "HaisosEngine.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include "src/components/libheaders/SanitizeUserInput.h"

namespace Haisos {

HaisosEngine::HaisosEngine(IFactory& factory)
    : m_factory(factory)
{
}

HaisosEngine::~HaisosEngine() = default;

std::string HaisosEngine::ReadFile(const std::string& filePath) {
    // Path traversal protection: normalize and ensure path stays within cwd
    try {
        std::filesystem::path absPath = std::filesystem::absolute(filePath);
        std::filesystem::path normPath = std::filesystem::weakly_canonical(absPath);
        std::filesystem::path cwd = std::filesystem::current_path();

        // Ensure normalized path starts with cwd (includes trailing separator check)
        auto normStr = normPath.native();
        auto cwdStr = cwd.native();
        if (normStr.size() < cwdStr.size() ||
            normStr.compare(0, cwdStr.size(), cwdStr) != 0 ||
            (normStr.size() > cwdStr.size() &&
             normStr[cwdStr.size()] != std::filesystem::path::preferred_separator)) {
            LogError("Invalid file path (path traversal attempt): %s", filePath.c_str());
            return "";
        }
    } catch (const std::exception& e) {
        LogError("Invalid file path (%s): %s", e.what(), filePath.c_str());
        return "";
    }

    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LogError("Failed to open file: %s", filePath.c_str());
        return "";
    }

    // File size limit: 10 MB
    const std::streamsize maxSize = 10 * 1024 * 1024;
    std::streamsize size = file.tellg();
    if (size > maxSize) {
        LogError("File too large: %s (%zd bytes, max %zd)", filePath.c_str(), static_cast<size_t>(size), static_cast<size_t>(maxSize));
        return "";
    }
    if (size < 0) {
        LogError("Failed to determine file size: %s", filePath.c_str());
        return "";
    }

    file.seekg(0, std::ios::beg);
    std::string content(static_cast<size_t>(size), '\0');
    if (!file.read(&content[0], size)) {
        LogError("Failed to read file: %s", filePath.c_str());
        return "";
    }
    return content;
}

void HaisosEngine::Run(const RunConfig& config, const SystemCallbacks& callbacks) {
    LogInfo("HaisosEngine starting with file: %s", config.userPrompt.c_str());

    std::string content;
    if (config.useFile) {
        content = ReadFile(config.userPrompt);
        if (content.empty()) {
            LogError("Failed to read file or file is empty");
            return;
        }
    } else {
        content = config.userPrompt;
        if (content.empty()) {
            LogError("User prompt is empty");
            return;
        }
    }

    std::string endpoint = std::getenv("HAISOS_ENDPOINT") ? std::getenv("HAISOS_ENDPOINT") : "http://localhost:11434/api/chat";
    std::string model = std::getenv("HAISOS_MODEL") ? std::getenv("HAISOS_MODEL") : "llama3";
    std::string apiKey = std::getenv("HAISOS_API_KEY") ? std::getenv("HAISOS_API_KEY") : "";

    auto console = m_factory.CreateConsole(false);
    console->Start();

    auto httpClient = m_factory.CreateHTTPClient();
    auto toolFactory = m_factory.CreateToolFactory(m_factory);
    auto llmCommunicator = m_factory.CreateLLMCommunicator(std::move(httpClient), endpoint, model, apiKey);

    std::vector<std::string> systemPrompts;
    if (!config.systemPrompt.empty()) {
        systemPrompts.push_back(config.systemPrompt);
    } else {
        systemPrompts.push_back("You are a helpful AI assistant.");
    }

    m_factory.SetSystemCallbacks(callbacks);
    auto agent = m_factory.CreateAgent(
        std::move(llmCommunicator),
        std::move(toolFactory),
        std::move(console),
        systemPrompts,
        "console_agent",
        nullptr,
        "",
        false);

    content = SanitizeUserInput(content);
    agent->Send(content);
    agent->Stop(0);
    agent->WaitToFinish();
}

}
