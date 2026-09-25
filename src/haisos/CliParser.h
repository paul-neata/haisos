#pragma once

#include <string>
#include <utility>
#include <vector>
#include "src/components/Logger/Logger.h"
#include "AgentTrafficLog.h"

namespace Haisos {

struct CliOptions {
    // Path to the haisosfile; empty means "use the default (./haisosfile)".
    std::string haisosFilePath;
    // "key=value" pairs given after a literal "--", fed to the haisosfile as
    // ARG overrides, in the order given.
    std::vector<std::pair<std::string, std::string>> argOverrides;

    bool logToConsole = false;
    std::string logFilePath;
    LogLevel logLevel = LogLevel::Warning;
    bool logJsonInTemp = false;
    // Where to write every agent's LLM traffic; empty means don't.
    std::string logAgentFilePath;
    AgentTrafficLogType logAgentFileType = AgentTrafficLogType::XDiff;
    bool help = false;
    bool version = false;
    // Write a commented starter haisosfile to ./haisosfile and exit.
    bool init = false;
};

struct ParseResult {
    CliOptions options;
    std::string error;
};

// Parses a log level name ("verbose_debug", "debug", "trace", "info",
// "warning", "error"). Returns false and leaves outLevel untouched if the name
// is not recognized, so callers can report the mistake instead of silently
// running at some other verbosity.
bool ParseLogLevel(const std::string& level, LogLevel& outLevel);

ParseResult ParseArguments(int argc, char* argv[]);

std::string FormatUsage(const char* programName);

} // namespace Haisos
