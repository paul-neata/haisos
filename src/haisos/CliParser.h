#pragma once

#include <string>
#include <utility>
#include <vector>
#include "src/components/Logger/Logger.h"

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
    bool help = false;
    bool version = false;
};

struct ParseResult {
    CliOptions options;
    std::string error;
};

LogLevel ParseLogLevel(const std::string& level);

ParseResult ParseArguments(int argc, char* argv[]);

std::string FormatUsage(const char* programName);

} // namespace Haisos
