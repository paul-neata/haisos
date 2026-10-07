#include "commands/hsh/HshBuiltins.h"

#include <optional>
#include <string>
#include <utility>

#include "commands/hsh/HshShell.h"
#include "commands/hsh/HshVariables.h"

namespace Haisos::Hsh {

namespace {

// Whether dash's cdcmd skips CDPATH for |dest|: it starts with '/', or its
// first component is exactly "." or "..".
bool SkipsCdPath(const std::string& dest) {
    if (dest.empty() || dest[0] == '/') {
        return true;
    }
    if (dest[0] != '.') {
        return false;
    }
    if (dest.size() >= 2 && dest[1] != '.') {
        return dest[1] == '/';  // "./...", but not ".foo"
    }
    if (dest.size() >= 3 && dest[2] != '.') {
        return dest[2] == '/';  // "../...", but not "..foo"; "..." goes
    }
    return dest.size() <= 2;  // "." and "..", never "..."
}

} // namespace

// cd [-L|-P] [dir], as dash's cdcmd: -L and -P are the same here (no symlinks
// yet), "-" is OLDPWD with the new directory printed, no directory is HOME. A
// relative destination whose first component is not "." or ".." is sought in
// CDPATH first (an empty entry is the working directory); found there, and
// not in a leading empty entry, the directory is printed. cd then sets OLDPWD
// and PWD, both exported, as dash's setpwd does.
int BuiltinCd(Shell& shell, const std::vector<std::string>& args) {
    size_t i = 1;
    for (; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--") {
            ++i;
            break;
        }
        if (arg.empty() || arg[0] != '-' || arg == "-") {
            break;  // "-" alone and everything else is the destination
        }
        for (size_t j = 1; j < arg.size(); ++j) {
            if (arg[j] != 'L' && arg[j] != 'P') {
                shell.Report(std::string("cd: Illegal option -") + arg[j]);
                return 2;
            }
        }
    }
    std::string dest;
    bool print = false;
    if (i >= args.size()) {
        // No directory: HOME; unset HOME is "" below, meaning ".".
        dest = shell.State().variables.Get("HOME").value_or("");
        if (dest.empty()) {
            dest = ".";
        }
    } else {
        dest = args[i];
        if (dest == "-") {
            dest = shell.State().variables.Get("OLDPWD").value_or("");
            print = true;
            if (dest.empty()) {
                dest = ".";
            }
        } else if (dest.empty()) {
            dest = ".";  // as dash: an empty destination is the working directory
        }
    }

    std::string where = dest;
    if (!SkipsCdPath(dest)) {
        // The CDPATH entries, split at ':'; an empty one is the working
        // directory. An entry that is there and a directory wins. dash prints
        // the new directory when c -- the first char of the remaining CDPATH
        // when this entry was taken -- is neither '\0' nor ':'; since an
        // empty entry means exactly c is '\0' or ':', that is: when the entry
        // is non-empty.
        const std::optional<std::string> cdpath = shell.State().variables.Get("CDPATH");
        if (cdpath) {
            size_t begin = 0;
            for (;;) {
                const size_t colon = cdpath->find(':', begin);
                const std::string entry = cdpath->substr(begin, colon == std::string::npos ? colon : colon - begin);
                const std::string candidate = entry.empty() ? dest
                    : entry + (entry.back() == '/' ? "" : "/") + dest;
                FileStatus status;
                if (shell.IO().Stat(candidate, status) == 0 && status.type == DirectoryEntryType::Dir) {
                    if (!entry.empty()) {
                        print = true;
                    }
                    where = candidate;
                    break;
                }
                if (colon == std::string::npos) {
                    break;
                }
                begin = colon + 1;
            }
        }
    }

    const std::string oldDirectory = shell.IO().GetCurrentDirectory();
    if (shell.IO().ChangeDirectory(where) != 0) {
        shell.Report("cd: can't cd to " + dest);
        return 2;
    }
    // dash's setpwd: OLDPWD, then PWD, each exported. A read-only one is
    // reported and left as it was -- the cd itself stands, as dash's does.
    bool readOnly = false;
    for (const std::pair<const char*, std::string>& assignment : {
             std::pair<const char*, std::string>{"OLDPWD", oldDirectory},
             std::pair<const char*, std::string>{"PWD", shell.IO().GetCurrentDirectory()}}) {
        if (shell.State().variables.IsReadonly(assignment.first)) {
            shell.Report(std::string("cd: ") + assignment.first + ": is read only");
            readOnly = true;
        } else {
            shell.AssignVariable(assignment.first, assignment.second);
            shell.State().variables.Export(assignment.first);
        }
    }
    if (print) {
        shell.WriteOut(shell.IO().GetCurrentDirectory() + "\n");
    }
    // Further operands are ignored, as dash ignores them.
    return readOnly ? 2 : 0;
}

} // namespace Haisos::Hsh
