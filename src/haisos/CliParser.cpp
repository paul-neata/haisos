#include "CliParser.h"

namespace Haisos {

std::string FormatUsage(const char* programName) {
    return std::string("Usage: ") + programName + " [haisosfile] [options] [-- key=value ...]\n"
        "\n"
        "haisosfile defaults to \"haisosfile\" in the current directory if omitted.\n"
        "Everything after a literal \"--\" is parsed as key=value pairs and fed to\n"
        "the haisosfile as ARG overrides.\n"
        "\n"
        "Options:\n"
        "      --log-to-console           Enable logging to console\n"
        "      --log-to-file <path>       Enable logging to file\n"
        "      --log-level <level>        Set log level (verbose_debug, debug, trace, info, warning, error)\n"
        "      --log-json-in-temp         Log input/output JSON to a temporary file\n"
        "      --log-agent-to-file <path> Write every agent's LLM traffic to <path>: each JSON\n"
        "                                 request sent (SEND) and response received (RECEIVE),\n"
        "                                 headed by the agent's name and the time\n"
        "      --log-agent-to-file-type <type>\n"
        "                                 How --log-agent-to-file writes it (default: diff):\n"
        "                                   diff  each request as its difference from the same\n"
        "                                         agent's previous one; responses in full\n"
        "                                   full  every request and response in full\n"
        "      --init                     Write a commented starter haisosfile to ./haisosfile and exit\n"
        "      --version                  Show version information\n"
        "  -h, --help                     Show this help message\n"
        "\nLLM configuration (read from the haisosfile's ENV directives; `ENV NAME`\n"
        "imports a host variable):\n"
        "  HAISOS_ENDPOINT    LLM API endpoint, e.g. http://localhost:11434/api/chat\n"
        "  HAISOS_MODEL       Model name\n"
        "  HAISOS_API_KEY     API key (optional for local Ollama)\n";
}

bool ParseLogLevel(const std::string& level, LogLevel& outLevel) {
    if (level == "verbose_debug") { outLevel = LogLevel::VerboseDebug; return true; }
    if (level == "debug") { outLevel = LogLevel::Debug; return true; }
    if (level == "trace") { outLevel = LogLevel::Trace; return true; }
    if (level == "info") { outLevel = LogLevel::Info; return true; }
    if (level == "warning") { outLevel = LogLevel::Warning; return true; }
    if (level == "error") { outLevel = LogLevel::Error; return true; }
    return false;
}

ParseResult ParseArguments(int argc, char* argv[]) {
    CliOptions options;
    std::string haisosFilePath;
    bool sawLogAgentFileType = false;

    int i = 1;
    for (; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--") {
            ++i;
            break;
        } else if (arg == "--help" || arg == "-h") {
            options.help = true;
            return ParseResult{options, ""};
        } else if (arg == "--version") {
            options.version = true;
            return ParseResult{options, ""};
        } else if (arg == "--init") {
            options.init = true;
            return ParseResult{options, ""};
        } else if (arg == "--log-to-console") {
            options.logToConsole = true;
        } else if (arg == "--log-to-file") {
            if (i + 1 < argc) {
                options.logFilePath = argv[++i];
            } else {
                return ParseResult{options, "Error: --log-to-file requires a path argument\n"};
            }
        } else if (arg == "--log-level") {
            if (i + 1 < argc) {
                std::string levelName = argv[++i];
                if (!ParseLogLevel(levelName, options.logLevel)) {
                    return ParseResult{options, "Error: unrecognized --log-level value: " + levelName + "\n"};
                }
            } else {
                return ParseResult{options, "Error: --log-level requires a level argument\n"};
            }
        } else if (arg == "--log-json-in-temp") {
            options.logJsonInTemp = true;
        } else if (arg == "--log-agent-to-file") {
            if (i + 1 < argc) {
                options.logAgentFilePath = argv[++i];
            } else {
                return ParseResult{options, "Error: --log-agent-to-file requires a path argument\n"};
            }
        } else if (arg == "--log-agent-to-file-type") {
            if (i + 1 < argc) {
                std::string typeName = argv[++i];
                if (!ParseAgentTrafficLogType(typeName, options.logAgentFileType)) {
                    return ParseResult{options, "Error: unrecognized --log-agent-to-file-type value: " + typeName + " (expected diff or full)\n"};
                }
                sawLogAgentFileType = true;
            } else {
                return ParseResult{options, "Error: --log-agent-to-file-type requires a type argument (diff or full)\n"};
            }
        } else if (!arg.empty() && arg[0] != '-') {
            if (!haisosFilePath.empty()) {
                return ParseResult{options, std::string("Error: Unexpected positional argument: ") + arg + "\n"};
            }
            haisosFilePath = arg;
        } else {
            return ParseResult{options, std::string("Error: Unknown flag: ") + arg + "\n"};
        }
    }

    for (; i < argc; ++i) {
        std::string arg = argv[i];
        auto eq = arg.find('=');
        if (eq == std::string::npos) {
            return ParseResult{options, std::string("Error: Expected key=value after --, got: ") + arg + "\n"};
        }
        options.argOverrides.emplace_back(arg.substr(0, eq), arg.substr(eq + 1));
    }

    // A type with nowhere to write it would do nothing, silently.
    if (sawLogAgentFileType && options.logAgentFilePath.empty()) {
        return ParseResult{options, "Error: --log-agent-to-file-type requires --log-agent-to-file <path>\n"};
    }

    options.haisosFilePath = haisosFilePath;

    return ParseResult{options, ""};
}

} // namespace Haisos
