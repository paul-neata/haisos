#include "BuiltinCommand.h"
#include <algorithm>

namespace Haisos {

BuiltinContext::BuiltinContext(
    ICurrentProcess& process,
    const IBuiltinCommand& command,
    const std::vector<std::string>& args,
    const std::atomic<bool>& stopRequested)
    : m_process(process)
    , m_io(process.IO())
    , m_name(command.Name())
    , m_version(command.Version())
    , m_args(args)
    , m_out(m_io ? m_io->GetDescriptor(IFileIO::kStdOut) : nullptr)
    , m_err(m_io ? m_io->GetDescriptor(IFileIO::kStdErr) : nullptr)
    , m_outIsTerminal(m_out && m_out->IsTerminal())
    , m_stopRequested(stopRequested)
{
}

BuiltinContext::~BuiltinContext() {
    Flush();
}

void BuiltinContext::Out(const std::string& text) {
    if (!m_out || m_outFailed) {
        return;
    }
    if (m_outIsTerminal) {
        // One write per Out: a partial line -- a prompt -- shows at once.
        if (text.empty()) {
            return;
        }
        if (!WriteAll(m_out.get(), text)) {
            m_outFailed = true;
        }
        return;
    }
    m_outBuffer += text;
    if (m_outBuffer.size() >= kBuiltinOutBufferSize) {
        Flush();
    }
}

void BuiltinContext::Error(const std::string& message) {
    // Output written so far comes first, as it would on a terminal showing
    // stdout and stderr together.
    Flush();
    WriteAll(m_err.get(), m_name + ": " + message + "\n");
}

void BuiltinContext::TryHelp() {
    Flush();
    WriteAll(m_err.get(), "Try '" + m_name + " --help' for more information.\n");
}

void BuiltinContext::ReportNotTreated(const ParsedBuiltinArgs& parsed) {
    for (const auto& option : parsed.options) {
        if (option.id == kBuiltinNotTreated) {
            NotTreated(option.spelling);
        }
    }
}

void BuiltinContext::NotTreated(const std::string& spelling) {
    if (std::find(m_reportedNotTreated.begin(), m_reportedNotTreated.end(), spelling) != m_reportedNotTreated.end()) {
        return;
    }
    m_reportedNotTreated.push_back(spelling);
    Flush();
    WriteAll(m_err.get(), "Parameter " + spelling + " is not treated by HaisosOS " + m_name + " v. " + m_version + "\n");
}

void BuiltinContext::Flush() {
    if (!m_out || m_outFailed || m_outBuffer.empty()) {
        m_outBuffer.clear();
        return;
    }
    std::string buffered;
    buffered.swap(m_outBuffer);
    if (!WriteAll(m_out.get(), buffered)) {
        m_outFailed = true;
    }
}

bool BuiltinContext::WriteAll(IFileDescriptor* descriptor, const std::string& bytes) {
    if (!descriptor) {
        return false;
    }
    size_t written = 0;
    while (written < bytes.size()) {
        const ssize_t result = descriptor->Write(bytes.data() + written, bytes.size() - written);
        if (result < 0) {
            return false;
        }
        written += static_cast<size_t>(result);
    }
    return true;
}

std::string BuiltinReferenceUrl(const std::string& name) {
    return "https://man7.org/linux/man-pages/man1/" + name + ".1.html";
}

std::string BuiltinVersionText(const IBuiltinCommand& command) {
    return command.Name() + " (HaisosOS builtin) " + command.Version() + "\n";
}

namespace {

const std::vector<BuiltinOption>& CommonOptions() {
    static const std::vector<BuiltinOption> common = {
        {0, "help", kBuiltinOptionHelp, BuiltinArgument::None, "", "show this help"},
        {0, "version", kBuiltinOptionVersion, BuiltinArgument::None, "", "show the version"},
    };
    return common;
}

// "-a, --all", "    --width=COLS", "-w, --width=COLS", "-m MODE" -- the left
// column of --help, as GNU lays it out.
std::string OptionSynopsis(const BuiltinOption& option) {
    std::string text = option.shortName ? std::string("-") + option.shortName : std::string("  ");
    if (!option.longName.empty()) {
        text += option.shortName ? ", --" : "  --";
        text += option.longName;
        if (option.argument == BuiltinArgument::Required) {
            text += "=" + option.argumentName;
        } else if (option.argument == BuiltinArgument::Optional) {
            text += "[=" + option.argumentName + "]";
        }
    } else if (option.argument != BuiltinArgument::None) {
        text += " " + option.argumentName;
    }
    return text;
}

// Every way an untreated option can be written: "-b, --escape".
std::string OptionSpellings(const BuiltinOption& option) {
    std::string text;
    if (option.shortName) {
        text = std::string("-") + option.shortName;
    }
    if (!option.longName.empty()) {
        text += (text.empty() ? "--" : ", --") + option.longName;
    }
    return text;
}

// Finds a long option by its exact name, or else by an unambiguous prefix of
// it, the way getopt_long does.
const BuiltinOption* FindLongOption(const std::string& name, const std::vector<BuiltinOption>& options, bool& ambiguous) {
    ambiguous = false;
    std::vector<const BuiltinOption*> prefixMatches;
    for (const auto* list : {&options, &CommonOptions()}) {
        for (const auto& option : *list) {
            if (option.longName.empty()) {
                continue;
            }
            if (option.longName == name) {
                return &option;
            }
            if (option.longName.compare(0, name.size(), name) == 0) {
                prefixMatches.push_back(&option);
            }
        }
    }
    if (prefixMatches.size() > 1) {
        ambiguous = true;
        return nullptr;
    }
    return prefixMatches.empty() ? nullptr : prefixMatches.front();
}

const BuiltinOption* FindShortOption(char name, const std::vector<BuiltinOption>& options) {
    for (const auto& option : options) {
        if (option.shortName == name) {
            return &option;
        }
    }
    return nullptr;
}

} // namespace

ParsedBuiltinArgs ParseBuiltinArgs(const std::vector<std::string>& args, const std::vector<BuiltinOption>& options) {
    ParsedBuiltinArgs parsed;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--") {
            parsed.operands.insert(parsed.operands.end(), args.begin() + static_cast<std::ptrdiff_t>(i) + 1, args.end());
            break;
        }
        if (arg.size() > 2 && arg.compare(0, 2, "--") == 0) {
            const size_t eq = arg.find('=');
            const std::string name = arg.substr(2, eq == std::string::npos ? std::string::npos : eq - 2);
            bool ambiguous = false;
            const BuiltinOption* option = FindLongOption(name, options, ambiguous);
            if (!option) {
                parsed.error = ambiguous
                    ? "option '" + arg + "' is ambiguous"
                    : "unrecognized option '" + arg + "'";
                return parsed;
            }
            ParsedBuiltinOption found;
            found.id = option->id;
            found.spelling = "--" + option->longName;
            if (eq != std::string::npos) {
                if (option->argument == BuiltinArgument::None) {
                    parsed.error = "option '--" + option->longName + "' doesn't allow an argument";
                    return parsed;
                }
                found.argument = arg.substr(eq + 1);
                found.hasArgument = true;
            } else if (option->argument == BuiltinArgument::Required) {
                if (i + 1 >= args.size()) {
                    parsed.error = "option '--" + option->longName + "' requires an argument";
                    return parsed;
                }
                found.argument = args[++i];
                found.hasArgument = true;
            }
            parsed.options.push_back(std::move(found));
            continue;
        }
        if (arg.size() > 1 && arg[0] == '-') {
            for (size_t j = 1; j < arg.size(); ++j) {
                const BuiltinOption* option = FindShortOption(arg[j], options);
                if (!option) {
                    parsed.error = std::string("invalid option -- '") + arg[j] + "'";
                    return parsed;
                }
                ParsedBuiltinOption found;
                found.id = option->id;
                found.spelling = std::string("-") + arg[j];
                if (option->argument == BuiltinArgument::Required) {
                    // The rest of the cluster is the argument ("-w80"), or
                    // else the next word is ("-w 80").
                    if (j + 1 < arg.size()) {
                        found.argument = arg.substr(j + 1);
                    } else if (i + 1 < args.size()) {
                        found.argument = args[++i];
                    } else {
                        parsed.error = std::string("option requires an argument -- '") + arg[j] + "'";
                        return parsed;
                    }
                    found.hasArgument = true;
                    parsed.options.push_back(std::move(found));
                    break;
                }
                parsed.options.push_back(std::move(found));
            }
            continue;
        }
        parsed.operands.push_back(arg);
    }
    return parsed;
}

std::string BuiltinHelpText(const IBuiltinCommand& command) {
    const BuiltinHelp help = command.Help();
    const std::string name = command.Name();
    std::string text = "HaisosOS " + name + " version " + command.Version() + " - " + help.summary + "\n";
    text += "Based on Linux " + name + ": " + BuiltinReferenceUrl(name) + "\n\n";

    for (size_t i = 0; i < help.usage.size(); ++i) {
        text += (i == 0 ? "Usage: " : "  or:  ") + help.usage[i] + "\n";
    }

    std::vector<const BuiltinOption*> treated;
    std::vector<const BuiltinOption*> notTreated;
    for (const auto& option : command.Options()) {
        (option.id == kBuiltinNotTreated ? notTreated : treated).push_back(&option);
    }
    for (const auto& option : CommonOptions()) {
        treated.push_back(&option);
    }
    constexpr size_t kDescriptionColumn = 30;
    text += "\n";
    for (const auto* option : treated) {
        std::string line = "  " + OptionSynopsis(*option);
        line += (line.size() + 2 <= kDescriptionColumn) ? std::string(kDescriptionColumn - line.size(), ' ') : "  ";
        text += line + option->description + "\n";
    }

    if (!help.notes.empty()) {
        text += "\n" + help.notes;
        if (help.notes.back() != '\n') {
            text += "\n";
        }
    }

    std::string notTreatedList;
    for (const auto* option : notTreated) {
        notTreatedList += (notTreatedList.empty() ? "" : ", ") + OptionSpellings(*option);
    }
    text += "\nNot treated arguments: " + (notTreatedList.empty() ? std::string("none") : notTreatedList) + "\n";
    return text;
}

std::optional<ParsedBuiltinArgs> BeginBuiltin(
    BuiltinContext& context, const IBuiltinCommand& command, int usageErrorStatus, int& exitStatus)
{
    ParsedBuiltinArgs parsed = ParseBuiltinArgs(context.Args(), command.Options());
    if (!parsed.error.empty()) {
        context.Error(parsed.error);
        context.TryHelp();
        exitStatus = usageErrorStatus;
        return std::nullopt;
    }
    // As getopt_long-based commands do, --help and --version win over
    // everything else given, whichever comes first.
    for (const auto& option : parsed.options) {
        if (option.id == kBuiltinOptionHelp) {
            context.Out(BuiltinHelpText(command));
            exitStatus = 0;
            return std::nullopt;
        }
        if (option.id == kBuiltinOptionVersion) {
            context.Out(BuiltinVersionText(command));
            exitStatus = 0;
            return std::nullopt;
        }
    }
    context.ReportNotTreated(parsed);
    return parsed;
}

} // namespace Haisos
