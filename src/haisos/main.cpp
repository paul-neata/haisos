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
#include "src/components/Filesystem/FilesystemUtils.h"
#include "interfaces/IFactory.h"
#include "interfaces/IServicesCreator.h"
#include "interfaces/IHaisosOS.h"
#include "CliParser.h"
#include "HaisosFileParser.h"
#include "HaisosFileSystemBuilder.h"

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

// Reads filePath relative to the current working directory, rejecting any
// path that escapes it and capping the read at 10 MB. Delegates the path
// resolution/escape-check and the read itself to PhysicalFileSystem (rooted
// at cwd) instead of re-validating paths by hand, so there is a single place
// ("does this path escape the root?") maintaining that logic.
// ReadWholeFile stops at 10MB and cannot report that it truncated, so a file
// that reaches the cap is rejected rather than parsed in part.
constexpr size_t kMaxHaisosFileSize = 10 * 1024 * 1024;

std::string ReadFileWithinCwd(IFactory& factory, const std::string& filePath) {
    // An absolute path is an explicit operator choice, so jail at the file's own
    // directory; a relative path is jailed to the cwd, where it still cannot
    // traverse out. Either way the jail root matches the directory that ROOT and
    // `FS ... PHYSICAL` are later resolved against, so content and root can't
    // come from different places.
    std::filesystem::path requested(filePath);
    std::string rootPath = ".";
    std::string nameInRoot = filePath;
    if (requested.is_absolute()) {
        rootPath = requested.parent_path().string();
        nameInRoot = requested.filename().string();
    }

    auto rootFileSystem = factory.CreatePhysicalFileSystem(rootPath);
    std::string content;
    if (!ReadWholeFile(*rootFileSystem, nameInRoot, content)) {
        LogError("Failed to open or read haisosfile (missing, unreadable, or outside %s): %s",
            rootPath.c_str(), filePath.c_str());
        return "";
    }
    if (content.size() >= kMaxHaisosFileSize) {
        LogError("haisosfile is too large (at least %zu bytes, max %zu): %s",
            content.size(), kMaxHaisosFileSize, filePath.c_str());
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

    if (result.options.init) {
        if (std::filesystem::exists("haisosfile")) {
            LogError("--init: haisosfile already exists in the current directory");
            std::cerr << "Error: haisosfile already exists in the current directory\n";
            return 1;
        }
        std::ofstream out("haisosfile");
        if (!out.is_open()) {
            LogError("--init: failed to create haisosfile in the current directory");
            std::cerr << "Error: failed to create haisosfile\n";
            return 1;
        }
        out << GetHaisosFileTemplate();
        out.close();
        std::cout << "Created haisosfile\n";
        return 0;
    }

    // Apply test environment variables for logging
    if (const char* envLevel = std::getenv("HAISOS_TEST_LOG_LEVEL")) {
        if (!ParseLogLevel(envLevel, result.options.logLevel)) {
            std::cerr << "Warning: ignoring unrecognized HAISOS_TEST_LOG_LEVEL value: " << envLevel << "\n";
        }
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

    // Set up console logging if requested via CLI
    // The Logger writes to the console on its own (defaulting to on), so this
    // flag just drives that switch. Registering a second, stdout-writing
    // receiver here would print every message twice.
    LogSetConsoleOutput(result.options.logToConsole);

    LogInfo("Haisos starting");

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

            static const std::string kRequestPrefix = "[JSON_REQUEST]";
            static const std::string kResponsePrefix = "[JSON_RESPONSE]";
            // The optional per-source tag (e.g. "[agent_1]") sits between the
            // prefix and the JSON payload, so find the payload by its leading
            // '{' rather than assuming a fixed offset.
            LogRegisterMessageReceiver([&tempJsonLog](const LogMessage& msg) {
                bool isRequest = msg.message.compare(0, kRequestPrefix.size(), kRequestPrefix) == 0;
                bool isResponse = !isRequest && msg.message.compare(0, kResponsePrefix.size(), kResponsePrefix) == 0;
                if (!isRequest && !isResponse) {
                    return;
                }
                auto jsonStart = msg.message.find('{');
                std::string json = (jsonStart == std::string::npos) ? "" : msg.message.substr(jsonStart);
                *tempJsonLog << "---- " << (isRequest ? "send" : "receive") << " ---- " << GetCurrentTimestamp() << "\n";
                *tempJsonLog << PrettyPrintJson(json) << "\n\n";
            });
        } else {
            LogWarning("Failed to open temporary JSON log file: %s", tempPath.c_str());
            tempJsonLog.reset();
        }
    }

    auto factory = CreateFactory();

    // Read the haisosfile (default: "haisosfile" in the current directory)
    std::string haisosFilePath = result.options.haisosFilePath.empty() ? "haisosfile" : result.options.haisosFilePath;
    std::string haisosFileContent = ReadFileWithinCwd(*factory, haisosFilePath);
    if (haisosFileContent.empty()) {
        LogError("Failed to read haisosfile or it is empty: %s", haisosFilePath.c_str());
        std::cerr << "Error: Failed to read haisosfile or it is empty: " << haisosFilePath << "\n";
        return 1;
    }

    auto parseResult = ParseHaisosFile(haisosFileContent, result.options.argOverrides);
    if (!parseResult.error.empty()) {
        LogError("Failed to parse haisosfile '%s': %s", haisosFilePath.c_str(), parseResult.error.c_str());
        std::cerr << parseResult.error;
        return 1;
    }

    // ROOT (and every FS PHYSICAL directive) is resolved relative to the
    // haisosfile's own directory.
    std::filesystem::path haisosFileDir = std::filesystem::absolute(haisosFilePath).parent_path();

    // The OS's environment comes only from the haisosfile's ENV directives: the
    // host's variables are not inherited wholesale, so `ENV NAME` is the single,
    // explicit way one gets in. The LLM configuration is read from here too.
    std::shared_ptr<IEnvironment> environment = factory->CreateEnvironment();
    for (const auto& env : parseResult.config.envEntries) {
        if (!env.importFromHost) {
            environment->SetVariable(env.name, env.value);
            continue;
        }
        if (const char* hostValue = std::getenv(env.name.c_str())) {
            environment->SetVariable(env.name, hostValue);
        } else {
            LogDebug("ENV %s: not set in the host environment, leaving it unset", env.name.c_str());
        }
    }

    // The key itself is deliberately never logged, only whether one was supplied.
    std::string endpoint = environment->GetVariable(kEnvEndpoint).value_or("(unset)");
    std::string model = environment->GetVariable(kEnvModel).value_or("(unset)");
    LogInfo("Haisos configuration: haisosfile='%s' endpoint='%s' model='%s' api_key_set=%d env_vars=%zu",
        haisosFilePath.c_str(),
        endpoint.c_str(),
        model.c_str(),
        environment->GetVariable(kEnvApiKey).value_or("").empty() ? 0 : 1,
        environment->GetVariableNames().size());

    std::shared_ptr<IServicesCreator> servicesCreator = factory->CreateServicesCreator();
    auto filesystemService = servicesCreator->CreateFileSystemService();

    std::string fsError;
    std::shared_ptr<IFileSystem> rootFileSystem = BuildRootFileSystem(*factory, *filesystemService, parseResult.config, haisosFileDir, fsError);
    if (!rootFileSystem) {
        LogError("Failed to build the root filesystem for '%s': %s", haisosFilePath.c_str(), fsError.c_str());
        std::cerr << fsError;
        return 1;
    }

    auto physicalConsole = factory->CreatePhysicalConsole(false);
    physicalConsole->Start();

    auto os = factory->CreateHaisosOS(servicesCreator, physicalConsole, rootFileSystem, environment);
    if (!os) {
        LogError("Failed to create the OS for '%s'", haisosFilePath.c_str());
        std::cerr << "Error: failed to create the OS\n";
        physicalConsole->Stop();
        return 1;
    }

    std::vector<std::shared_ptr<IProcess>> processes;
    for (const auto& runEntry : parseResult.config.runEntries) {
        // Each RUN gets its own copy of the OS's environment: nothing is
        // inherited implicitly, and one process's edits never reach another's.
        auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), runEntry.programPath, runEntry.args);
        if (!process) {
            LogError("Failed to start process: %s", runEntry.programPath.c_str());
            std::cerr << "Error: Failed to start process: " << runEntry.programPath << "\n";
            continue;
        }
        processes.push_back(process);
    }

    if (processes.empty()) {
        LogError("No process could be started from '%s'; nothing to run", haisosFilePath.c_str());
        std::cerr << "Error: no process could be started from " << haisosFilePath << "\n";
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
