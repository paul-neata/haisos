#include "BuiltinCommand.h"
#include <string>
#include <vector>
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {

namespace {

constexpr int kOptionIgnoreFailures = 1;  // --ignore-fail-on-non-empty
constexpr int kOptionParents = 2;         // -p, --parents
constexpr int kOptionVerbose = 3;         // -v, --verbose

// GNU's quoteaf: every name in a removal message, quoted even when plain.
std::string q(const std::string& name) {
    return ShellEscapeQuoted(name, /*always=*/true);
}

// How one removal went: Ignored is a failure --ignore-fail-on-non-empty
// covers (silent, and no failure for the exit status).
enum class Removal { Removed, Ignored, Failed };

// Why an IFileIO removal failed, since it gives no reason: looked up with
// Stat and ReadDirectory, as rmdir's errno would.
std::string RemovalReason(IFileIO& io, const std::string& dir) {
    FileStatus status;
    if (io.Stat(dir, status) != 0) {
        return "No such file or directory";
    }
    if (status.type != DirectoryEntryType::Dir) {
        return "Not a directory";
    }
    for (const auto& entry : io.ReadDirectory(dir)) {
        if (entry.name != "." && entry.name != "..") {
            return "Directory not empty";
        }
    }
    // A mount point, the root of a filesystem, a read-only filesystem too.
    return "Device or resource busy";
}

// Attempts to remove |dir|, saying so first with -v. The failure message of a
// parent (-p) carries the word "directory"; the operand's does not, as GNU's.
Removal RemoveOne(BuiltinContext& context, const std::string& dir, bool isParent,
                  bool ignoreFailures, bool verbose) {
    if (verbose) {
        context.Out("rmdir: removing directory, " + q(dir) + "\n");
    }
    std::string reason;
    std::string trimmed = dir;
    while (trimmed.size() > 1 && trimmed.back() == '/') {
        trimmed.pop_back();
    }
    if (VirtualLastSegment(trimmed) == ".") {
        // rmdir(".") is EINVAL, before anything is touched.
        reason = "Invalid argument";
    } else if (context.IO().RemoveDirectory(dir) == 0) {
        return Removal::Removed;
    } else {
        reason = RemovalReason(context.IO(), dir);
    }
    if (ignoreFailures && reason == "Directory not empty") {
        return Removal::Ignored;
    }
    context.Error(std::string(isParent ? "failed to remove directory " : "failed to remove ")
        + q(dir) + ": " + reason);
    return Removal::Failed;
}

class RmdirCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "rmdir"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {0, "ignore-fail-on-non-empty", kOptionIgnoreFailures, BuiltinArgument::None, "", "ignore each failure to remove a non-empty directory"},
            {'p', "parents", kOptionParents, BuiltinArgument::None, "", "remove DIRECTORY and its ancestors; e.g. 'rmdir -p a/b/c' removes 'a/b/c', 'a/b' and 'a'"},
            {'v', "verbose", kOptionVerbose, BuiltinArgument::None, "", "output a diagnostic for every directory processed"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "remove empty directories",
            {"rmdir [OPTION]... DIRECTORY..."},
            "A failed removal of an empty directory is reported as 'Device or resource busy':\n"
            "no reason is known (a read-only filesystem included)."};
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }
        bool ignoreFailures = false;
        bool parents = false;
        bool verbose = false;
        for (const auto& option : parsed->options) {
            switch (option.id) {
                case kOptionIgnoreFailures: ignoreFailures = true; break;
                case kOptionParents: parents = true; break;
                case kOptionVerbose: verbose = true; break;
                default: break;  // not treated (already reported)
            }
        }
        if (parsed->operands.empty()) {
            context.Error("missing operand");
            context.TryHelp();
            return 1;
        }

        int status = 0;
        for (const auto& operand : parsed->operands) {
            if (!RemoveWithParents(context, operand, parents, ignoreFailures, verbose)) {
                status = 1;
            }
        }
        return status;
    }

private:
    // One operand: the directory itself, then (with -p) each ancestor,
    // stopping at the first removal that did not happen.
    static bool RemoveWithParents(BuiltinContext& context, const std::string& operand,
                                 bool parents, bool ignoreFailures, bool verbose) {
        const Removal own = RemoveOne(context, operand, /*isParent=*/false, ignoreFailures, verbose);
        if (own != Removal::Removed) {
            // Failed is already reported; Ignored is silent, and GNU does not
            // walk on to the parents of a directory it did not remove.
            return own == Removal::Ignored;
        }
        if (!parents) {
            return true;
        }
        std::string prefix = operand;
        while (prefix.size() > 1 && prefix.back() == '/') {
            prefix.pop_back();
        }
        while (true) {
            const size_t slash = prefix.find_last_of('/');
            if (slash == std::string::npos || slash == 0) {
                break;  // nothing (or only "/") is left
            }
            prefix.resize(slash);
            const Removal removal = RemoveOne(context, prefix, /*isParent=*/true, ignoreFailures, verbose);
            if (removal != Removal::Removed) {
                return removal == Removal::Ignored;
            }
        }
        return true;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateRmdirCommand() {
    return std::make_shared<RmdirCommand>();
}

} // namespace Haisos