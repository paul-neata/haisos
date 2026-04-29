#include <iostream>
#include <string>
#include <memory>
#include <cstdlib>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <iterator>
#include <random>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "src/components/Factory/Factory.h"
#include "src/components/Logger/Logger.h"
#include "interfaces/IFactory.h"
#include "CliParser.h"

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

    // Create factory
    auto factory = CreateFactory();

    // Create Haisos engine
    auto engine = factory->CreateHaisosEngine(*factory);

    // Prepare callbacks
    SystemCallbacks callbacks;
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
            callbacks.on_send = [&tempJsonLog](const std::string& json) {
                *tempJsonLog << "---- send ---- " << GetCurrentTimestamp() << "\n";
                *tempJsonLog << PrettyPrintJson(json) << "\n\n";
            };
            callbacks.on_received = [&tempJsonLog](const std::string& json) {
                *tempJsonLog << "---- receive ---- " << GetCurrentTimestamp() << "\n";
                *tempJsonLog << PrettyPrintJson(json) << "\n\n";
            };
        } else {
            LogWarning("Failed to open temporary JSON log file: %s", tempPath.c_str());
            tempJsonLog.reset();
        }
    }

    // Read system prompt from file if requested
    std::string systemPrompt = result.options.systemPrompt;
    if (!result.options.systemPromptFile.empty()) {
        try {
            std::filesystem::path absPath = std::filesystem::absolute(result.options.systemPromptFile);
            std::filesystem::path normPath = std::filesystem::weakly_canonical(absPath);
            std::filesystem::path cwd = std::filesystem::current_path();

            auto normStr = normPath.native();
            auto cwdStr = cwd.native();
            if (normStr.size() < cwdStr.size() ||
                normStr.compare(0, cwdStr.size(), cwdStr) != 0 ||
                (normStr.size() > cwdStr.size() &&
                 normStr[cwdStr.size()] != std::filesystem::path::preferred_separator)) {
                std::cerr << "Error: Invalid system prompt file path (path traversal attempt): "
                          << result.options.systemPromptFile << "\n";
                return 1;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error: Invalid system prompt file path (" << e.what()
                      << "): " << result.options.systemPromptFile << "\n";
            return 1;
        }

        std::ifstream file(result.options.systemPromptFile);
        if (!file.is_open()) {
            std::cerr << "Error: Failed to open system prompt file: " << result.options.systemPromptFile << "\n";
            return 1;
        }
        systemPrompt = std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    // Read prompt from stdin if requested
    std::string userPrompt = result.options.userPrompt;
    if (result.options.takeStdin) {
        userPrompt = std::string(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
    }

    // Build RunConfig
    RunConfig config;
    config.userPrompt = userPrompt;
    config.useFile = result.options.useFile;
    config.systemPrompt = systemPrompt;

    // Run the engine
    engine->Run(config, callbacks);

    if (tempJsonLog) {
        tempJsonLog->close();
    }

    LogInfo("Haisos finished");
    return 0;
}
