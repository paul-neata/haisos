#pragma once

#include <string>
#include <vector>

#include "commands/hsh/HshVariables.h"

namespace Haisos::Hsh {

// One of dash's options: its letter (0 when it has only a name), its -o name,
// and the ShellOptions field it sets -- null when hsh does not act on it
// (it is then reported as not treated, and not stored).
struct ShellOptionInfo {
    char letter;
    const char* name;
    bool ShellOptions::* field;
};

// dash's options, in the order `set -o` lists them: errexit e, noglob f,
// ignoreeof I, interactive i, monitor m, noexec n, stdin s, xtrace x,
// verbose v, vi V, emacs E, noclobber C, allexport a, notify b, nounset u,
// privileged p, nolog (no letter), debug (no letter). Treated (field set):
// e f i n s x C a u. Not treated: I m v V E b p nolog debug.
const std::vector<ShellOptionInfo>& ShellOptionTable();
const ShellOptionInfo* FindShellOption(char letter);               // null if none
const ShellOptionInfo* FindShellOption(const std::string& name);   // null if none

// What `hsh` was asked to run.
struct Invocation {
    enum class Source { CommandString, ScriptFile, StandardInput };
    Source source = Source::StandardInput;
    std::string commandString;            // -c
    std::string scriptPath;               // the command_file operand
    std::string arg0 = "hsh";             // $0, and what messages start with
    std::vector<std::string> positional;  // $1 ...
    ShellOptions options;
    std::vector<std::string> notTreated;  // "-m", "+V", "-o monitor", "-l": to report
    std::string error;                    // dash's message after "hsh: 0: ", e.g. "Illegal option -y"
};

// dash's procargs: the arguments after the command name, up to the first
// operand (or "--" / a lone "-"), parse as options; the rest say what to run.
Invocation ParseInvocation(const std::vector<std::string>& args);

} // namespace Haisos::Hsh
