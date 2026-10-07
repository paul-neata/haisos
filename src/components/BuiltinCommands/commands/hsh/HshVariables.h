#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "IEnvironment.h"

namespace Haisos::Hsh {

// The shell's named variables. Names are valid shell names (IsValidShellName);
// callers check before calling.
class ShellVariables {
public:
    // Every variable of |environment| whose name is a valid shell name, set
    // and exported -- what a shell does with the environment it starts with.
    void ImportFrom(const IEnvironment& environment);

    std::optional<std::string> Get(const std::string& name) const;  // nullopt when unset
    bool IsSet(const std::string& name) const;
    // Sets the value, creating the variable; false, changing nothing, when it is read-only.
    bool Set(const std::string& name, const std::string& value);
    // Removes it with its flags; false when it is read-only; true when it was not there.
    bool Unset(const std::string& name);

    // export NAME: marks it exported, set or not (an unset one is exported once set).
    void Export(const std::string& name);
    bool IsExported(const std::string& name) const;
    // readonly NAME: from then on Set and Unset fail.
    void MakeReadonly(const std::string& name);
    bool IsReadonly(const std::string& name) const;

    // Every name that is set or carries a flag, sorted by byte (for `set`,
    // `export -p`, `readonly -p`).
    std::vector<std::string> Names() const;
    // Name and value of every set, exported variable, sorted by name: what a
    // command the shell starts gets as its environment.
    std::vector<std::pair<std::string, std::string>> ExportedVariables() const;

private:
    struct Entry {
        std::optional<std::string> value;
        bool exported = false;
        bool readonly = false;
    };
    std::map<std::string, Entry> m_variables;
};

// dash's options (set -e, ...).
struct ShellOptions {
    bool errexit = false;      // -e
    bool noglob = false;       // -f
    bool interactive = false;  // -i
    bool noexec = false;       // -n
    bool stdinInput = false;   // -s
    bool xtrace = false;       // -x
    bool verbose = false;      // -v
    bool noclobber = false;    // -C
    bool allexport = false;    // -a
    bool nounset = false;      // -u
};
// $-: the letters of the options that are on, in dash's order "uaCvxsnife"
// (dash -euaCfx -c 'echo $-' prints uaCxfe).
std::string OptionLetters(const ShellOptions& options);

// Everything a running shell knows that expansions read. The executor owns
// one; a subshell works on a copy.
struct ShellState {
    ShellVariables variables;
    ShellOptions options;
    std::string arg0 = "hsh";                   // $0
    std::vector<std::string> positional;        // $1, $2, ...
    int lastExitStatus = 0;                     // $?
    uint64_t shellPid = 0;                      // $$
    std::optional<uint64_t> lastBackgroundPid;  // $!: unset until a command runs in the background
};

} // namespace Haisos::Hsh
