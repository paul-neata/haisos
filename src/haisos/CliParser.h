#pragma once

#include <string>
#include <vector>
#include "src/components/Logger/Logger.h"

namespace Haisos {

struct CliOptions {
    std::string userPrompt;
    std::string systemPrompt;
    std::string systemPromptFile;
    bool useFile = false;
    bool logToConsole = false;
    std::string logFilePath;
    LogLevel logLevel = LogLevel::Warning;
    bool logJsonInTemp = false;
    bool takeStdin = false;
    bool help = false;
    bool version = false;
};

struct ParseResult {
    CliOptions options;
    std::string error;
};

ParseResult ParseArguments(int argc, char* argv[]);

std::string FormatUsage(const char* programName);

} // namespace Haisos
