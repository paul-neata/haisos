#include "commands/hsh/HshInvocation.h"

namespace Haisos::Hsh {

const std::vector<ShellOptionInfo>& ShellOptionTable() {
    static const std::vector<ShellOptionInfo> table = {
        {'e', "errexit", &ShellOptions::errexit},
        {'f', "noglob", &ShellOptions::noglob},
        {'I', "ignoreeof", nullptr},
        {'i', "interactive", &ShellOptions::interactive},
        {'m', "monitor", nullptr},
        {'n', "noexec", &ShellOptions::noexec},
        {'s', "stdin", &ShellOptions::stdinInput},
        {'x', "xtrace", &ShellOptions::xtrace},
        {'v', "verbose", nullptr},
        {'V', "vi", nullptr},
        {'E', "emacs", nullptr},
        {'C', "noclobber", &ShellOptions::noclobber},
        {'a', "allexport", &ShellOptions::allexport},
        {'b', "notify", nullptr},
        {'u', "nounset", &ShellOptions::nounset},
        {'p', "privileged", nullptr},
        {0, "nolog", nullptr},
        {0, "debug", nullptr},
    };
    return table;
}

const ShellOptionInfo* FindShellOption(char letter) {
    for (const ShellOptionInfo& info : ShellOptionTable()) {
        if (info.letter == letter) {
            return &info;
        }
    }
    return nullptr;
}

const ShellOptionInfo* FindShellOption(const std::string& name) {
    for (const ShellOptionInfo& info : ShellOptionTable()) {
        if (info.name == name) {
            return &info;
        }
    }
    return nullptr;
}

Invocation ParseInvocation(const std::vector<std::string>& args) {
    Invocation invocation;
    bool commandStringFlag = false;  // -c seen
    size_t i = 0;
    while (i < args.size()) {
        const std::string& arg = args[i];
        if (arg.empty() || (arg[0] != '-' && arg[0] != '+')) {
            break;  // an operand: the options end
        }
        if (arg == "--" || arg == "-") {
            ++i;  // consumed; the options end
            break;
        }
        if (arg == "+") {
            break;  // not an option: an operand
        }
        const bool on = arg[0] == '-';
        if (on && arg.size() > 2 && arg[1] == '-') {
            invocation.error = "Illegal option --";
            return invocation;
        }
        for (size_t j = 1; j < arg.size(); ++j) {
            const char letter = arg[j];
            if (letter == 'c') {
                commandStringFlag = true;  // +c sets it too, as dash
                continue;
            }
            if (letter == 'l') {
                invocation.notTreated.push_back(std::string(on ? "-" : "+") + "l");
                continue;
            }
            if (letter == 'o') {
                // The option name is the next argument; the rest of this one,
                // if any, is ignored, as dash ignores it.
                if (i + 1 >= args.size()) {
                    invocation.error = "-o requires an argument";
                    return invocation;
                }
                const std::string name = args[++i];
                const ShellOptionInfo* info = FindShellOption(name);
                if (!info) {
                    invocation.error = std::string("Illegal option ") + (on ? "-o " : "+o ") + name;
                    return invocation;
                }
                if (!info->field) {
                    invocation.notTreated.push_back(std::string(on ? "-o " : "+o ") + name);
                } else {
                    invocation.options.*(info->field) = on;
                }
                break;
            }
            const ShellOptionInfo* info = FindShellOption(letter);
            if (!info) {
                invocation.error = std::string("Illegal option ") + (on ? "-" : "+") + letter;
                return invocation;
            }
            if (!info->field) {
                invocation.notTreated.push_back(std::string(on ? "-" : "+") + letter);
            } else {
                invocation.options.*(info->field) = on;
            }
        }
        ++i;
    }
    if (commandStringFlag) {
        if (i >= args.size()) {
            invocation.error = "-c requires an argument";
            return invocation;
        }
        invocation.commandString = args[i++];
        if (i < args.size()) {
            invocation.arg0 = args[i++];
        }
        invocation.positional.assign(args.begin() + static_cast<ptrdiff_t>(i), args.end());
        invocation.source = Invocation::Source::CommandString;
        return invocation;
    }
    if (invocation.options.stdinInput || i >= args.size()) {
        invocation.source = Invocation::Source::StandardInput;
        invocation.options.stdinInput = true;
        invocation.positional.assign(args.begin() + static_cast<ptrdiff_t>(i), args.end());
        return invocation;
    }
    invocation.scriptPath = args[i++];
    invocation.arg0 = invocation.scriptPath;
    invocation.positional.assign(args.begin() + static_cast<ptrdiff_t>(i), args.end());
    invocation.source = Invocation::Source::ScriptFile;
    return invocation;
}

} // namespace Haisos::Hsh
