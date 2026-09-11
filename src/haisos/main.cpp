#include <iostream>
#include <string>
#include <memory>
#include <cstdlib>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <random>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "src/components/Factory/Factory.h"
#include "src/components/ServicesCreator/ServicesCreator.h"
#include "src/components/HaisosOS/HaisosOS.h"
#include "src/components/Logger/Logger.h"
#include "interfaces/IFactory.h"
#include "interfaces/IServicesCreator.h"
#include "interfaces/IHaisosOS.h"
#include "CliParser.h"
#include "HaisosFileParser.h"

using namespace Haisos;

#ifndef HAISOS_VERSION
#define HAISOS_VERSION "unknown"
#endif
constexpr const char* VERSION_STRING = HAISOS_VERSION;

std::string GetCurrentTimestamp() {
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

std::string PrettyPrintJson(const std::string& jsonStr) {
    try {
        auto j = nlohmann::json::parse(jsonStr, nullptr, false);
        return j.dump(2);
    } catch (...) {
        return jsonStr;
    }
}

// Path traversal protection: normalize and ensure the path stays within cwd.
// TODO(haisosfile): superseded once the OS's rooted PhysicalFileSystem lands.
std::string ReadFileWithinCwd(const std::string& filePath) {
    try {
        std::filesystem::path absPath = std::filesystem::absolute(filePath);
        std::filesystem::path normPath = std::filesystem::weakly_canonical(absPath);
        std::filesystem::path cwd = std::filesystem::current_path();

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

int main(int argc, char* argv[]) {
    auto result = ParseArguments(argc, argv);

    if (!result.error.empty()) {
        std::cerr << result.error;
        std::cerr << FormatUsage(argv[0]);
        return 1;
    }

    if (result.options.help) {
        std::cout << FormatUsage(argv[0]);
        return 0;
    }

    if (result.options.version) {
        std::cout << "Haisos version " << VERSION_STRING << std::endl;
        return 0;
    }

    // Apply test environment variables for logging
    if (const char* envLevel = std::getenv("HAISOS_TEST_LOG_LEVEL")) {
        result.options.logLevel = ParseLogLevel(envLevel);
    }

    // Set minimum log level
    LogSetMinimumLevel(result.options.logLevel);

    // Set up file logging if requested via CLI or environment
    std::unique_ptr<std::ofstream> logFileStream;
    if (!result.options.logFilePath.empty()) {
        logFileStream = std::make_unique<std::ofstream>(result.options.logFilePath, std::ios::out | std::ios::trunc);
    } else if (const char* envFile = std::getenv("HAISOS_TEST_LOG_FILE")) {
        logFileStream = std::make_unique<std::ofstream>(envFile, std::ios::out | std::ios::app);
    }

    if (logFileStream && logFileStream->is_open()) {
        LogRegisterMessageReceiver([&logFileStream](const LogMessage& msg) {
            const char* levelStr =
                msg.level == LogLevel::VerboseDebug ? "VERBOSE_DEBUG" :
                msg.level == LogLevel::Debug ? "DEBUG" :
                msg.level == LogLevel::Trace ? "TRACE" :
                msg.level == LogLevel::Info ? "INFO" :
                msg.level == LogLevel::Warning ? "WARNING" :
                msg.level == LogLevel::Error ? "ERROR" : "UNKNOWN";
            *logFileStream << "[" << msg.timestamp << "][" << levelStr << "] " << msg.message << "\n" << std::flush;
        });
    }

    LogInfo("Haisos starting with model from environment");

    // Raw LLM JSON traffic is logged by LLMCommunicator at VerboseDebug level, tagged
    // "[JSON_REQUEST] "/"[JSON_RESPONSE] ". --log-json-in-temp taps that via a log
    // receiver instead of a bespoke callback mechanism.
    std::unique_ptr<std::ofstream> tempJsonLog;
    if (result.options.logJsonInTemp) {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dist;
        std::string tempPath = "/tmp/haisos_" + std::to_string(ms) + "_" + std::to_string(dist(gen)) + ".jsonlog";
        tempJsonLog = std::make_unique<std::ofstream>(tempPath, std::ios::out | std::ios::trunc);
        if (tempJsonLog->is_open()) {
            LogInfo("Logging JSON traffic to temporary file: %s", tempPath.c_str());
            std::cout << "Logging JSON traffic to temporary file: " << tempPath << std::endl;

            // The JSON trace log lines are only emitted at VerboseDebug; make sure
            // they actually fire even if --log-level asked for something coarser.
            if (LogGetMinimumLevel() > LogLevel::VerboseDebug) {
                LogSetMinimumLevel(LogLevel::VerboseDebug);
            }

            static const std::string kRequestPrefix = "[JSON_REQUEST] ";
            static const std::string kResponsePrefix = "[JSON_RESPONSE] ";
            LogRegisterMessageReceiver([&tempJsonLog](const LogMessage& msg) {
                if (msg.message.compare(0, kRequestPrefix.size(), kRequestPrefix) == 0) {
                    *tempJsonLog << "---- send ---- " << GetCurrentTimestamp() << "\n";
                    *tempJsonLog << PrettyPrintJson(msg.message.substr(kRequestPrefix.size())) << "\n\n";
                } else if (msg.message.compare(0, kResponsePrefix.size(), kResponsePrefix) == 0) {
                    *tempJsonLog << "---- receive ---- " << GetCurrentTimestamp() << "\n";
                    *tempJsonLog << PrettyPrintJson(msg.message.substr(kResponsePrefix.size())) << "\n\n";
                }
            });
        } else {
            LogWarning("Failed to open temporary JSON log file: %s", tempPath.c_str());
            tempJsonLog.reset();
        }
    }

    // Read the haisosfile (default: "haisosfile" in the current directory)
    std::string haisosFilePath = result.options.haisosFilePath.empty() ? "haisosfile" : result.options.haisosFilePath;
    std::string haisosFileContent = ReadFileWithinCwd(haisosFilePath);
    if (haisosFileContent.empty()) {
        LogError("Failed to read haisosfile or it is empty: %s", haisosFilePath.c_str());
        std::cerr << "Error: Failed to read haisosfile or it is empty: " << haisosFilePath << "\n";
        return 1;
    }

    auto parseResult = ParseHaisosFile(haisosFileContent, result.options.argOverrides);
    if (!parseResult.error.empty()) {
        std::cerr << parseResult.error;
        return 1;
    }

    // ROOT is resolved relative to the haisosfile's own directory; an omitted
    // ROOT defaults to that same directory.
    std::filesystem::path haisosFileDir = std::filesystem::absolute(haisosFilePath).parent_path();
    std::string rootPath = parseResult.config.rootPath.empty()
        ? haisosFileDir.string()
        : (haisosFileDir / parseResult.config.rootPath).string();

    std::string endpoint = std::getenv("HAISOS_ENDPOINT") ? std::getenv("HAISOS_ENDPOINT") : "http://localhost:11434/api/chat";
    std::string model = std::getenv("HAISOS_MODEL") ? std::getenv("HAISOS_MODEL") : "llama3";
    std::string apiKey = std::getenv("HAISOS_API_KEY") ? std::getenv("HAISOS_API_KEY") : "";

    auto factory = CreateFactory();
    auto servicesCreator = CreateServicesCreator(*factory);

    std::shared_ptr<INetworkService> networkService = servicesCreator->CreateNetworkService();
    std::shared_ptr<ILLMService> llmService = servicesCreator->CreateLLMService(*networkService, endpoint, model, apiKey);
    auto filesystem = factory->CreatePhysicalFileSystem(rootPath);
    auto filesystemService = servicesCreator->CreateFileSystemService(std::move(filesystem));
    auto physicalConsole = factory->CreatePhysicalConsole(false);
    physicalConsole->Start();

    auto os = CreateHaisosOS(*factory, *servicesCreator, networkService, llmService, std::move(filesystemService), rootPath, physicalConsole, true);

    std::vector<std::shared_ptr<IProcess>> processes;
    for (const auto& runEntry : parseResult.config.runEntries) {
        auto process = os->StartProcess(runEntry.programPath, runEntry.args, nullptr);
        if (!process) {
            LogError("Failed to start process: %s", runEntry.programPath.c_str());
            std::cerr << "Error: Failed to start process: " << runEntry.programPath << "\n";
            continue;
        }
        processes.push_back(process);
    }

    if (processes.empty()) {
        physicalConsole->Stop();
        return 1;
    }

    // The haisos process finishes once all of its initial processes have.
    for (const auto& process : processes) {
        process->WaitToFinish();
    }

    physicalConsole->Stop();

    if (tempJsonLog) {
        tempJsonLog->close();
    }

    LogInfo("Haisos finished");
    return 0;
}
