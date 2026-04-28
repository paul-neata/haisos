#include "CliParser.h"

namespace Haisos {

std::string FormatUsage(const char* programName) {
    return std::string("Usage: ") + programName + " [options] [<markdown_file>]\n"
        "Options:\n"
        "  -f, --file <path>              Specify the markdown file to process\n"
        "  -p, --prompt <string>          Provide the user prompt directly as a string\n"
        "      --system-prompt <string>   Set a system prompt text\n"
        "      --system-prompt-file <path> Read system prompt from a file\n"
        "      --take-stdin               Read the entire user prompt from stdin until EOF\n"
        "      --log-to-console           Enable logging to console\n"
        "      --log-to-file <path>       Enable logging to file\n"
        "      --log-level <level>        Set log level (verbose_debug, debug, trace, info, warning, error)\n"
        "      --log-json-in-temp         Log input/output JSON to a temporary file\n"
        "      --version                  Show version information\n"
        "  -h, --help                     Show this help message\n"
        "\nEnvironment variables:\n"
        "  HAISOS_ENDPOINT   LLM API endpoint (default: http://localhost:11434/api/chat)\n"
        "  HAISOS_MODEL       Model name (default: llama3)\n"
        "  HAISOS_API_KEY     API key (optional for local Ollama)\n";
}

static LogLevel ParseLogLevel(const std::string& level) {
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
    std::string mdFilePath;
    std::string prompt;
    std::string systemPrompt;
    std::string systemPromptFile;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            options.help = true;
            return ParseResult{options, ""};
        } else if (arg == "--version") {
            options.version = true;
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
        } else if (arg == "--file" || arg == "-f") {
            if (i + 1 < argc) {
                mdFilePath = argv[++i];
            } else {
                return ParseResult{options, "Error: --file requires a path argument\n"};
            }
        } else if (arg == "--prompt" || arg == "-p") {
            if (i + 1 < argc) {
                prompt = argv[++i];
            } else {
                return ParseResult{options, "Error: --prompt requires a string argument\n"};
            }
        } else if (arg == "--system-prompt") {
            if (i + 1 < argc) {
                systemPrompt = argv[++i];
            } else {
                return ParseResult{options, "Error: --system-prompt requires a string argument\n"};
            }
        } else if (arg == "--system-prompt-file") {
            if (i + 1 < argc) {
                systemPromptFile = argv[++i];
            } else {
                return ParseResult{options, "Error: --system-prompt-file requires a path argument\n"};
            }
        } else if (arg == "--take-stdin") {
            options.takeStdin = true;
        } else if (arg[0] != '-') {
            if (!mdFilePath.empty()) {
                return ParseResult{options, std::string("Error: Unexpected positional argument: ") + arg + "\n"};
            }
            mdFilePath = arg;
        } else {
            return ParseResult{options, std::string("Error: Unknown flag: ") + arg + "\n"};
        }
    }

    // Validate arguments
    if (prompt.empty() && !options.takeStdin && mdFilePath.empty()) {
        return ParseResult{options, "Error: No input specified. Provide a markdown file, --prompt, or --take-stdin\n"};
    }

    if (!prompt.empty() && options.takeStdin) {
        return ParseResult{options, "Error: --prompt and --take-stdin cannot be used together\n"};
    }

    if (!mdFilePath.empty()) {
        options.userPrompt = mdFilePath;
        options.useFile = true;
    } else if (!prompt.empty()) {
        options.userPrompt = prompt;
        options.useFile = false;
    }

    options.systemPrompt = systemPrompt;
    options.systemPromptFile = systemPromptFile;

    return ParseResult{options, ""};
}

} // namespace Haisos
