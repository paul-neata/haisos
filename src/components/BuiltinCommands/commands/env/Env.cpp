#include "BuiltinCommand.h"
#include "BuiltinRunProgram.h"
#include "BuiltinText.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace Haisos {

namespace {

constexpr int kOptionIgnoreEnvironment = 1;  // -i
constexpr int kOptionNull = 2;               // -0
constexpr int kOptionUnset = 3;              // -u NAME
constexpr int kOptionChdir = 4;              // -C DIR
constexpr int kOptionSplitString = 5;         // -S STRING

// What one step of -S splitting did: went on, ended the string (\c), or
// failed (reported).
enum class SplitStep { Ok, Stop, Error };

class EnvCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "env"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'i', "ignore-environment", kOptionIgnoreEnvironment, BuiltinArgument::None, "",
                "start with an empty environment"},
            {'0', "null", kOptionNull, BuiltinArgument::None, "",
                "end each output line with NUL, not newline"},
            {'u', "unset", kOptionUnset, BuiltinArgument::Required, "NAME",
                "remove variable NAME from the environment"},
            {'C', "chdir", kOptionChdir, BuiltinArgument::Required, "DIR",
                "change working directory to DIR"},
            {'S', "split-string", kOptionSplitString, BuiltinArgument::Required, "S",
                "process and split S into separate arguments"},
            {0, "block-signal", kBuiltinNotTreated, BuiltinArgument::Optional, "SIG"},
            {0, "default-signal", kBuiltinNotTreated, BuiltinArgument::Optional, "SIG"},
            {0, "ignore-signal", kBuiltinNotTreated, BuiltinArgument::Optional, "SIG"},
            {0, "list-signal-handling", kBuiltinNotTreated},
            {'v', "debug", kBuiltinNotTreated},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "run a program in a modified environment",
            {"env [OPTION]... [-] [NAME=VALUE]... [COMMAND [ARG]...]"},
            "With no COMMAND, every variable is printed sorted by name (GNU prints\n"
            "them in the environment's own order, which HaisosOS keeps none of).\n"
            "-i empties the variables only: secrets and LLM identifiers stay.\n"
            "Signal options are not treated: HaisosOS processes have no signals.\n"
            "-S: words separated by spaces and tabs; '...' and \"...\" quoting;\n"
            "backslash escapes of \\ \" ' t n r v f # $ _ (a space between words, a\n"
            "literal space inside \"...\") and c (the rest of the string ignored);\n"
            "${NAME} expanded (unset: empty); # at the start of a word a comment."};
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        // GNU's "+" mode: options end at the first operand, so COMMAND and its
        // arguments, and every NAME=VALUE after the first non-assignment, are
        // never options. Usage errors are GNU env's 125.
        auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/125, exitStatus,
            /*stopAtFirstOperand=*/true);
        if (!parsed) {
            return exitStatus;
        }
        // -S rewrites the arguments; the parse is then of the rewritten ones.
        if (!RewriteSplitStrings(context, *parsed)) {
            return 125;
        }
        for (const auto& option : parsed->options) {
            if (option.id == kBuiltinOptionHelp) {
                context.Out(BuiltinHelpText(*this));
                return 0;
            }
            if (option.id == kBuiltinOptionVersion) {
                context.Out(BuiltinVersionText(*this));
                return 0;
            }
        }

        // The environment the child gets: the process's own clone, edited by
        // the options in the order they were given.
        std::shared_ptr<IEnvironment> environment = context.Process().GetEnvironment();
        bool nullSeparated = false;
        std::string chdir;
        for (const auto& option : parsed->options) {
            switch (option.id) {
                case kOptionIgnoreEnvironment:
                    for (const auto& name : environment->GetVariableNames()) {
                        environment->RemoveVariable(name);
                    }
                    break;
                case kOptionNull:
                    nullSeparated = true;
                    break;
                case kOptionUnset:
                    if (option.argument.empty() || option.argument.find('=') != std::string::npos) {
                        context.Error("cannot unset " + ShellEscapeQuoted(option.argument, true)
                            + ": Invalid argument");
                        return 125;
                    }
                    environment->RemoveVariable(option.argument);
                    break;
                case kOptionChdir:
                    chdir = option.argument;
                    break;
                default:
                    break;
            }
        }

        // Operands: a leading "-" means -i, then every NAME=VALUE sets a
        // variable, until the first operand that is none: that is COMMAND.
        std::vector<std::string> operands = parsed->operands;
        size_t next = 0;
        if (next < operands.size() && operands[next] == "-") {
            for (const auto& name : environment->GetVariableNames()) {
                environment->RemoveVariable(name);
            }
            ++next;
        }
        for (; next < operands.size(); ++next) {
            const std::string& operand = operands[next];
            const size_t eq = operand.find('=');
            if (eq == std::string::npos || eq == 0) {
                break;
            }
            environment->SetVariable(operand.substr(0, eq), operand.substr(eq + 1));
        }
        const std::vector<std::string> command(operands.begin() + static_cast<std::ptrdiff_t>(next),
                                                operands.end());

        if (command.empty()) {
            if (!chdir.empty()) {
                context.Error("must specify command with --chdir (-C)");
                context.TryHelp();
                return 125;
            }
            std::vector<std::string> names = environment->GetVariableNames();
            std::sort(names.begin(), names.end());
            std::string out;
            for (const auto& name : names) {
                out += name + "=" + environment->GetVariable(name).value_or("");
                out += nullSeparated ? '\0' : '\n';
            }
            context.Out(out);
            return 0;
        }
        if (nullSeparated) {
            context.Error("cannot specify --null (-0) with command");
            context.TryHelp();
            return 125;
        }

        std::optional<std::string> workingDirectory;
        if (!chdir.empty()) {
            FileStatus status;
            if (context.IO().Stat(chdir, status) != 0) {
                context.Error("cannot change directory to " + ShellEscapeQuoted(chdir, true)
                    + ": No such file or directory");
                return 125;
            }
            if (status.type != DirectoryEntryType::Dir) {
                context.Error("cannot change directory to " + ShellEscapeQuoted(chdir, true)
                    + ": Not a directory");
                return 125;
            }
            workingDirectory = chdir;
        }

        // The command is looked up in the *new* environment's PATH, as execvp
        // does after env has set it.
        const std::string& name = command.front();
        if (name.find('/') != std::string::npos) {
            FileStatus status;
            if (context.IO().Stat(name, status) == 0 && status.type == DirectoryEntryType::Dir) {
                context.Error(ShellEscapeQuoted(name, true) + ": Permission denied");
                return 126;
            }
        }
        const std::optional<std::string> programPath = FindProgramInPath(context, name, environment.get());
        if (!programPath) {
            context.Error(ShellEscapeQuoted(name, true) + ": No such file or directory");
            return 127;
        }
        bool started = false;
        const int code = RunProgramAndWait(context, *programPath,
            std::vector<std::string>(command.begin() + 1, command.end()),
            RunProgramOptions{nullptr, nullptr, nullptr, workingDirectory, environment}, &started);
        if (!started) {
            context.Error(ShellEscapeQuoted(name, true) + ": Permission denied");
            return 126;
        }
        return code;
    }

private:
    // -S, GNU's rules in the subset HaisosOS needs. One round: every option
    // except the -S ones re-spelled (spelling, then its argument as its own
    // word), then the words of each -S string, then the operands -- and the
    // parse run again on the result, while a -S remains (at most 16 rounds).
    // Returns false on a splitting or re-parsing error (reported), with |parsed|
    // left as the last good parse.
    bool RewriteSplitStrings(BuiltinContext& context, ParsedBuiltinArgs& parsed) {
        bool hasSplit = false;
        for (int round = 0; round < 16; ++round) {
            hasSplit = false;
            for (const auto& option : parsed.options) {
                if (option.id == kOptionSplitString) {
                    hasSplit = true;
                }
            }
            if (!hasSplit) {
                return true;
            }
            std::vector<std::string> rebuilt;
            for (const auto& option : parsed.options) {
                if (option.id == kOptionSplitString) {
                    continue;
                }
                rebuilt.push_back(option.spelling);
                if (option.hasArgument) {
                    rebuilt.push_back(option.argument);
                }
            }
            for (const auto& option : parsed.options) {
                if (option.id == kOptionSplitString && !SplitString(context, option.argument, rebuilt)) {
                    return false;
                }
            }
            rebuilt.insert(rebuilt.end(), parsed.operands.begin(), parsed.operands.end());
            ParsedBuiltinArgs reparsed = ParseBuiltinArgs(rebuilt, Options(), /*stopAtFirstOperand=*/true);
            if (!reparsed.error.empty()) {
                context.Error(reparsed.error);
                context.TryHelp();
                return false;
            }
            context.ReportNotTreated(reparsed);
            parsed = std::move(reparsed);
        }
        return true;
    }

    // Splits one -S string into words, appended to |out|. Returns false on an
    // error (reported).
    bool SplitString(BuiltinContext& context, const std::string& text, std::vector<std::string>& out) {
        const std::shared_ptr<IEnvironment> environment = context.Process().GetEnvironment();
        std::string word;
        bool inWord = false;
        const auto appendWord = [&]() {
            if (inWord) {
                out.push_back(word);
                word.clear();
                inWord = false;
            }
        };
        size_t i = 0;
        bool stopped = false;  // \c: the rest of the string is ignored
        while (i < text.size() && !stopped) {
            const char c = text[i];
            if (c == ' ' || c == '\t') {
                appendWord();
                ++i;
                continue;
            }
            if (c == '#' && !inWord) {
                break;  // a comment runs to the end of the string
            }
            if (c == '\'') {
                // Literal: nothing is special inside, backslashes included.
                const size_t close = text.find('\'', i + 1);
                if (close == std::string::npos) {
                    context.Error("no terminating quote in -S string");
                    return false;
                }
                word.append(text, i + 1, close - i - 1);
                inWord = true;
                i = close + 1;
                continue;
            }
            if (c == '"') {
                inWord = true;
                ++i;
                bool closed = false;
                while (i < text.size() && !closed && !stopped) {
                    if (text[i] == '"') {
                        closed = true;
                        ++i;
                    } else if (text[i] == '\\') {
                        switch (AppendEscape(context, text, i, /*inQuotes=*/true, word, appendWord)) {
                            case SplitStep::Error: return false;
                            case SplitStep::Stop: stopped = true; break;
                            case SplitStep::Ok: break;
                        }
                    } else if (text[i] == '$') {
                        if (!AppendExpansion(text, i, *environment, word)) {
                            context.Error("only ${VARNAME} expansion is supported, error at: "
                                + text.substr(i));
                            return false;
                        }
                    } else {
                        word += text[i];
                        ++i;
                    }
                }
                if (!closed && !stopped) {
                    context.Error("no terminating quote in -S string");
                    return false;
                }
                continue;
            }
            if (c == '\\') {
                switch (AppendEscape(context, text, i, /*inQuotes=*/false, word, appendWord)) {
                    case SplitStep::Error: return false;
                    case SplitStep::Stop: stopped = true; break;
                    case SplitStep::Ok: break;
                }
                continue;
            }
            if (c == '$') {
                if (!AppendExpansion(text, i, *environment, word)) {
                    context.Error("only ${VARNAME} expansion is supported, error at: " + text.substr(i));
                    return false;
                }
                inWord = true;
                continue;
            }
            word += c;
            inWord = true;
            ++i;
        }
        appendWord();
        return true;
    }

    // One backslash escape at text[i] == '\\', inside double quotes or not.
    // Advances i past it, appending to |word| (or ending the word, for \_
    // outside quotes). Stop is \c: the rest of the string is ignored. Error is
    // an invalid sequence (reported).
    SplitStep AppendEscape(BuiltinContext& context, const std::string& text, size_t& i, bool inQuotes,
                           std::string& word, const std::function<void()>& appendWord) {
        if (i + 1 >= text.size()) {
            context.Error("invalid sequence '\\' in -S");
            return SplitStep::Error;
        }
        const char code = text[i + 1];
        i += 2;
        switch (code) {
            case '\\': word += '\\'; break;
            case '"': word += '"'; break;
            case '\'': word += '\''; break;
            case 't': word += '\t'; break;
            case 'n': word += '\n'; break;
            case 'r': word += '\r'; break;
            case 'v': word += '\v'; break;
            case 'f': word += '\f'; break;
            case '#': word += '#'; break;
            case '$': word += '$'; break;
            case '_':
                // Outside quotes a word separator; inside "..." a space.
                if (!inQuotes) {
                    appendWord();
                } else {
                    word += ' ';
                }
                break;
            case 'c':
                return SplitStep::Stop;
            default:
                context.Error(std::string("invalid sequence '\\") + code + " in -S");
                return SplitStep::Error;
        }
        return SplitStep::Ok;
    }

    // ${NAME} at text[i] == '$': expanded from |environment| (unset: empty).
    // Returns false on anything else a $ could start.
    static bool AppendExpansion(const std::string& text, size_t& i, const IEnvironment& environment,
                                std::string& word) {
        if (i + 1 >= text.size() || text[i + 1] != '{') {
            return false;
        }
        const size_t close = text.find('}', i + 2);
        if (close == std::string::npos) {
            return false;
        }
        const std::string name = text.substr(i + 2, close - i - 2);
        word += environment.GetVariable(name).value_or("");
        i = close + 1;
        return true;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateEnvCommand() {
    return std::make_shared<EnvCommand>();
}

} // namespace Haisos