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
        "      --init                     Write a commented starter haisosfile to ./haisosfile and exit\n"
        "      --version                  Show version information\n"
        "  -h, --help                     Show this help message\n"
        "\nEnvironment variables:\n"
        "  HAISOS_ENDPOINT   LLM API endpoint (default: http://localhost:11434/api/chat)\n"
        "  HAISOS_MODEL       Model name (default: llama3)\n"
        "  HAISOS_API_KEY     API key (optional for local Ollama)\n";
}

LogLevel ParseLogLevel(const std::string& level) {
    if (level == "verbose_debug") return LogLevel::VerboseDebug;
    if (level == "debug") return LogLevel::Debug;
    if (level == "trace") return LogLevel::Trace;
    if (level == "info") return LogLevel::Info;
    if (level == "warning") return LogLevel::Warning;
    if (level == "error") return LogLevel::Error;
    return LogLevel::Info;
}

ParseResult ParseArguments(int argc, char* argv[]) {
    CliOptions options;
    std::string haisosFilePath;

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
                options.logLevel = ParseLogLevel(argv[++i]);
            } else {
                return ParseResult{options, "Error: --log-level requires a level argument\n"};
            }
        } else if (arg == "--log-json-in-temp") {
            options.logJsonInTemp = true;
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

    options.haisosFilePath = haisosFilePath;

    return ParseResult{options, ""};
}

} // namespace Haisos
