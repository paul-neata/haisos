#include <optional>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinCopy.h"
#include "BuiltinPrompt.h"
#include "BuiltinRemove.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/Filesystem/VirtualPath.h"

namespace Haisos {

namespace {

constexpr int kOptionBackup = 1;          // --backup[=CONTROL]
constexpr int kOptionBackupShort = 2;      // -b
constexpr int kOptionForce = 3;            // -f
constexpr int kOptionInteractive = 4;      // -i
constexpr int kOptionNoClobber = 5;        // -n
constexpr int kOptionNoCopy = 6;           // --no-copy
constexpr int kOptionStripTrailingSlashes = 7;
constexpr int kOptionSuffix = 8;           // -S
constexpr int kOptionTargetDirectory = 9;  // -t
constexpr int kOptionNoTargetDirectory = 10; // -T
constexpr int kOptionUpdate = 11;          // --update[=UPDATE]
constexpr int kOptionUpdateShort = 12;     // -u
constexpr int kOptionVerbose = 13;         // -v

// The source's last segment with trailing slashes stripped: "." and ".." are
// what GNU refuses to move.
std::string LastSegmentOf(const std::string& path) {
    std::string trimmed = path;
    while (trimmed.size() > 1 && trimmed.back() == '/') {
        trimmed.pop_back();
    }
    return VirtualLastSegment(trimmed);
}

// Whether ReadDirectory(path) lists anything besides "." and "..".
bool HasEntries(IFileIO& io, const std::string& path) {
    for (const auto& entry : io.ReadDirectory(path)) {
        if (entry.name != "." && entry.name != "..") {
            return true;
        }
    }
    return false;
}

// One source and its destination through GNU mv's checks and moves, in one
// step where the filesystem allows it and by copy-then-remove across two
// filesystems (see BuiltinCopy and BuiltinRemove). |prompt| may be null when
// |interactive| is false. Returns false on any failure (the exit status then
// 1).
bool MoveTarget(BuiltinContext& context, BuiltinPrompt* prompt, const CopyTarget& target,
                bool interactive, bool noCopy, bool verbose, UpdateMode update,
                BackupMode backupMode, const std::string& backupSuffix) {
    IFileIO& io = context.IO();
    const std::string& source = target.source;
    const std::string& dest = target.dest;

    FileStatus sourceStatus;
    if (io.Stat(source, sourceStatus) != 0) {
        context.Error("cannot stat " + CopyQuoted(source) + ": " + CopyStatMissingReason(io, source));
        return false;
    }

    const std::string lastSegment = LastSegmentOf(source);
    if (lastSegment == "." || lastSegment == "..") {
        context.Error("cannot move " + CopyQuoted(source) + " to " + CopyQuoted(dest)
            + ": Device or resource busy");
        return false;
    }

    const std::string resolvedSource = io.ResolvePath(source);
    const std::string resolvedDest = io.ResolvePath(dest);
    if (resolvedSource == resolvedDest) {
        context.Error(CopyQuoted(source) + " and " + CopyQuoted(dest) + " are the same file");
        return false;
    }

    if (sourceStatus.type == DirectoryEntryType::Dir) {
        const std::string below = resolvedSource == "/" ? "/" : resolvedSource + "/";
        if (resolvedDest.size() > below.size() &&
            resolvedDest.compare(0, below.size(), below) == 0) {
            context.Error("cannot move " + CopyQuoted(source) + " to a subdirectory of itself, "
                + CopyQuoted(dest));
            return false;
        }
    }

    if (io.IsBuiltinCommand(source)) {
        context.Error("cannot move " + CopyQuoted(source) + " to " + CopyQuoted(dest)
            + ": Permission denied");
        return false;
    }

    FileStatus destStatus;
    const bool destExists = io.Stat(dest, destStatus) == 0;
    std::string backupPath;
    if (destExists) {
        if (sourceStatus.type == DirectoryEntryType::Dir && destStatus.type != DirectoryEntryType::Dir) {
            context.Error("cannot overwrite non-directory " + CopyQuoted(dest) + " with directory "
                + CopyQuoted(source));
            return false;
        }
        if (sourceStatus.type != DirectoryEntryType::Dir && destStatus.type == DirectoryEntryType::Dir) {
            context.Error("cannot overwrite directory " + CopyQuoted(dest) + " with non-directory");
            return false;
        }
        if (sourceStatus.type == DirectoryEntryType::Dir && destStatus.type == DirectoryEntryType::Dir &&
            HasEntries(io, dest)) {
            context.Error("cannot overwrite " + CopyQuoted(dest) + ": Directory not empty");
            return false;
        }

        // An existing destination that would not be replaced: -n and
        // --update=none say so and fail, -u skips the newer one silently.
        if (update == UpdateMode::None) {
            context.Error("not replacing " + CopyQuoted(dest));
            return false;
        }
        if (update == UpdateMode::Older &&
            destStatus.modificationTime >= sourceStatus.modificationTime) {
            return true;
        }
        if (interactive && prompt) {
            if (!prompt->Ask(context.Name() + ": overwrite " + CopyQuoted(dest) + "? ")) {
                return false;
            }
        }

        // The backup: the old destination is renamed aside before the move.
        if (backupMode != BackupMode::None) {
            backupPath = BackupPathFor(context, dest, backupMode, backupSuffix);
            if (io.Rename(dest, backupPath) != 0) {
                context.Error("cannot backup " + CopyQuoted(dest) + ": Permission denied");
                return false;
            }
        }
    }

    const int result = io.Rename(source, dest);
    if (result == 0) {
        if (verbose) {
            std::string line = "renamed " + CopyQuoted(source) + " -> " + CopyQuoted(dest);
            if (!backupPath.empty()) {
                line += " (backup: " + CopyQuoted(backupPath) + ")";
            }
            context.Out(line + "\n");
        }
        return true;
    }

    if (result == kFileSystemCrossDevice) {
        // Across filesystems (across a mount), as GNU mv does across
        // devices: copy, then remove the source. The copy keeps
        // modification and access times only (no modes, owners or links
        // exist), and its own -v lines; the removal adds rm's.
        if (noCopy) {
            context.Error("cannot move " + CopyQuoted(source) + " to " + CopyQuoted(dest)
                + ": Invalid cross-device link");
            return false;
        }
        CopyOptions copyOptions;
        copyOptions.recursive = true;
        copyOptions.preserveTimes = true;
        copyOptions.verbose = verbose ? CopyVerbose::Mv : CopyVerbose::None;
        if (!CopyPath(context, nullptr, source, dest, copyOptions)) {
            return false;
        }
        RemoveOptions removeOptions;
        removeOptions.recursive = true;
        removeOptions.verbose = verbose;
        removeOptions.preserveRoot = true;
        return RemoveOperand(context, nullptr, source, removeOptions);
    }

    // Any other failure; IFileIO gives no reason, so find one.
    const std::string parent = VirtualParentOf(resolvedDest);
    const auto parentType = EntryTypeOf(io, parent);
    const char* reason = !parentType ? "No such file or directory"
        : (*parentType != DirectoryEntryType::Dir ? "Not a directory" : "Permission denied");
    context.Error("cannot move " + CopyQuoted(source) + " to " + CopyQuoted(dest) + ": " + reason);
    return false;
}

class MvCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "mv"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {0, "backup", kOptionBackup, BuiltinArgument::Optional, "CONTROL", "make a backup of each existing destination file (none, simple, existing or numbered)"},
            {'b', "", kOptionBackupShort, BuiltinArgument::None, "", "like --backup but does not accept an argument"},
            {0, "debug", kBuiltinNotTreated},
            {'f', "force", kOptionForce, BuiltinArgument::None, "", "do not prompt before overwriting"},
            {'i', "interactive", kOptionInteractive, BuiltinArgument::None, "", "prompt before overwrite"},
            {'n', "no-clobber", kOptionNoClobber, BuiltinArgument::None, "", "do not overwrite an existing file"},
            {0, "no-copy", kOptionNoCopy, BuiltinArgument::None, "", "refuse to copy instead of moving across filesystems"},
            {0, "strip-trailing-slashes", kOptionStripTrailingSlashes, BuiltinArgument::None, "", "remove any trailing slashes from each SOURCE"},
            {'S', "suffix", kOptionSuffix, BuiltinArgument::Required, "SUFFIX", "override the usual backup suffix"},
            {'t', "target-directory", kOptionTargetDirectory, BuiltinArgument::Required, "DIRECTORY", "move all SOURCE arguments into DIRECTORY"},
            {'T', "no-target-directory", kOptionNoTargetDirectory, BuiltinArgument::None, "", "treat DEST as a normal file"},
            {0, "update", kOptionUpdate, BuiltinArgument::Optional, "UPDATE", "replace only older files: all, none or older"},
            {'u', "", kOptionUpdateShort, BuiltinArgument::None, "", "equivalent to --update[=older]"},
            {'v', "verbose", kOptionVerbose, BuiltinArgument::None, "", "explain what is being done"},
            {'Z', "context", kBuiltinNotTreated},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "move (rename) files",
            {
                "mv [OPTION]... [-T] SOURCE DEST",
                "mv [OPTION]... SOURCE... DIRECTORY",
                "mv [OPTION]... -t DIRECTORY SOURCE...",
            },
            "Across filesystems (across a mount) the copy keeps modification and\n"
            "access times only: no modes, owners or links exist.\n"
            "A builtin command cannot be moved."};
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }

        bool interactive = false;
        bool noCopy = false;
        bool stripTrailingSlashes = false;
        bool verbose = false;
        bool noClobbered = false;  // -n wins over a later --update=WORD
        UpdateMode update = UpdateMode::All;
        BackupRequest backup;
        std::optional<std::string> targetDirectory;
        bool noTargetDirectory = false;

        for (const auto& option : parsed->options) {
            switch (option.id) {
                case kOptionBackup:
                    backup.on = true;
                    if (option.hasArgument) {
                        if (!ParseBackupControl(context, option.argument, backup.mode)) {
                            return 1;
                        }
                        backup.wordSeen = true;
                    }
                    break;
                case kOptionBackupShort: backup.on = true; break;
                case kOptionForce:
                    interactive = false;
                    if (noClobbered) {
                        // -f after -n replaces again: the last of the three
                        // wins, as GNU's rule.
                        update = UpdateMode::All;
                        noClobbered = false;
                    }
                    break;
                case kOptionInteractive:
                    interactive = true;
                    if (noClobbered) {
                        // -i after -n prompts again, as GNU.
                        update = UpdateMode::All;
                        noClobbered = false;
                    }
                    break;
                case kOptionNoClobber:
                    update = UpdateMode::None;
                    interactive = false;
                    noClobbered = true;
                    break;
                case kOptionNoCopy: noCopy = true; break;
                case kOptionStripTrailingSlashes: stripTrailingSlashes = true; break;
                case kOptionSuffix:
                    backup.on = true;  // a suffix alone backs up, as GNU 9.4
                    backup.suffix = option.argument;
                    break;
                case kOptionTargetDirectory: targetDirectory = option.argument; break;
                case kOptionNoTargetDirectory: noTargetDirectory = true; break;
                case kOptionUpdate: {
                    UpdateMode word = UpdateMode::Older;
                    if (option.hasArgument && !ParseUpdateWord(context, option.argument, word)) {
                        return 1;
                    }
                    if (!noClobbered) {
                        update = word;
                    }
                    break;
                }
                case kOptionUpdateShort:
                    if (!noClobbered) {
                        update = UpdateMode::Older;
                    }
                    break;
                case kOptionVerbose: verbose = true; break;
                default: break;  // not treated (already reported)
            }
        }

        // The backup mode is worked out here, once, as GNU does: -b and -S
        // take $VERSION_CONTROL's word, --backup=WORD its own.
        if (!FinishBackupRequest(context, backup)) {
            return 1;
        }
        const BackupMode backupMode = backup.on ? backup.mode : BackupMode::None;
        const std::string backupSuffix = backup.on ? backup.suffix : "~";

        const auto targets = ResolveCopyTargets(context, parsed->operands, targetDirectory,
                                                noTargetDirectory, /*parents=*/false,
                                                stripTrailingSlashes);
        if (!targets) {
            return 1;
        }

        BuiltinPrompt prompt(context);
        int status = 0;
        for (const auto& target : *targets) {
            if (context.StopRequested()) {
                return 1;
            }
            if (!MoveTarget(context, interactive ? &prompt : nullptr, target,
                            interactive, noCopy, verbose, update, backupMode, backupSuffix)) {
                status = 1;
            }
        }
        return status;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateMvCommand() {
    return std::make_shared<MvCommand>();
}

} // namespace Haisos