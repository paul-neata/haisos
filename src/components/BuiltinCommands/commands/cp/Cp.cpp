#include <optional>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinCopy.h"
#include "BuiltinPrompt.h"
#include "BuiltinText.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/Filesystem/VirtualPath.h"

namespace Haisos {

namespace {

constexpr int kOptionArchive = 1;          // -a
constexpr int kOptionAttributesOnly = 2;    // --attributes-only
constexpr int kOptionBackup = 3;            // --backup[=CONTROL]
constexpr int kOptionBackupShort = 4;       // -b
constexpr int kOptionNoDereference = 5;     // -d
constexpr int kOptionForce = 6;             // -f
constexpr int kOptionInteractive = 7;       // -i
constexpr int kOptionFollowCommandLine = 8; // -H
constexpr int kOptionLink = 9;              // -l
constexpr int kOptionDereference = 10;      // -L
constexpr int kOptionNoClobber = 11;        // -n
constexpr int kOptionNeverDereference = 12; // -P
constexpr int kOptionPreserve = 13;         // -p
constexpr int kOptionPreserveList = 14;     // --preserve[=ATTR_LIST]
constexpr int kOptionNoPreserve = 15;       // --no-preserve=ATTR_LIST
constexpr int kOptionParents = 16;          // --parents
constexpr int kOptionRecursive = 17;         // -R and -r
constexpr int kOptionRemoveDestination = 18; // --remove-destination
constexpr int kOptionStripTrailingSlashes = 19;
constexpr int kOptionSymbolicLink = 20;      // -s
constexpr int kOptionSuffix = 21;            // -S
constexpr int kOptionTargetDirectory = 22;   // -t
constexpr int kOptionNoTargetDirectory = 23; // -T
constexpr int kOptionUpdate = 24;            // --update[=UPDATE]
constexpr int kOptionUpdateShort = 25;       // -u
constexpr int kOptionVerbose = 26;           // -v

#ifdef _WIN32
constexpr int kDirMode = _S_IREAD | _S_IWRITE;
#else
constexpr int kDirMode = S_IRWXU | S_IRWXG | S_IRWXO;
#endif

// GNU's quoteaf: every name in a message, quoted even when plain.
std::string q(const std::string& name) {
    return ShellEscapeQuoted(name, /*always=*/true);
}

// Joins |dir| and |name| with no doubled slash ("/" + "x" is "/x").
std::string JoinPath(const std::string& dir, const std::string& name) {
    std::string joined = dir;
    if (!joined.empty() && joined.back() != '/') {
        joined += '/';
    }
    return joined + name;
}

// Why creating |path| failed (BuiltinCopy's rule): "No such file or
// directory" when its directory is missing, "Not a directory" when that is a
// file, else -- a builtin's path, a read-only filesystem -- "Permission
// denied" (IFileIO gives no reason).
std::string CreateFailedReason(IFileIO& io, const std::string& path) {
    const std::string parent = VirtualParentOf(io.ResolvePath(path));
    auto parentType = EntryTypeOf(io, parent);
    if (!parentType) {
        return "No such file or directory";
    }
    if (*parentType != DirectoryEntryType::Dir) {
        return "Not a directory";
    }
    return "Permission denied";
}

// The segments of |path| split on '/', as written: empty segments dropped,
// "." and ".." kept (the destination is built from the source literally).
std::vector<std::string> LiteralSegments(const std::string& path) {
    std::vector<std::string> segments;
    std::string current;
    for (char c : path) {
        if (c == '/') {
            if (!current.empty()) {
                segments.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        segments.push_back(current);
    }
    return segments;
}

// --parents: creates the directories along the source's path under the
// destination, printing each one made (unquoted, with -v) as GNU does:
// "docs -> p/docs", "docs/sub -> p/docs/sub". The copied name itself is
// CopyPath's business -- for a directory source it creates it with its own
// line, exactly as GNU.
bool MakeParents(BuiltinContext& context, const CopyTarget& target, bool verbose) {
    IFileIO& io = context.IO();
    const std::vector<std::string> source = LiteralSegments(target.source);
    const std::vector<std::string> dest = LiteralSegments(target.dest);
    // The destination is the target directory with the source's path (its
    // leading '/' dropped) under it, so its tail segments are the source's.
    if (source.size() < 2 || dest.size() < source.size()) {
        return true;
    }
    const size_t extra = dest.size() - source.size();
    std::string sourcePrefix = target.source[0] == '/' ? "/" : "";
    std::string destPrefix = target.dest[0] == '/' ? "/" : "";
    for (size_t j = 0; j < extra; ++j) {
        destPrefix = destPrefix.empty() ? dest[j] : JoinPath(destPrefix, dest[j]);
    }
    for (size_t i = 0; i + 1 < source.size(); ++i) {
        sourcePrefix = sourcePrefix.empty() ? source[i] : JoinPath(sourcePrefix, source[i]);
        destPrefix = destPrefix.empty() ? dest[extra + i] : JoinPath(destPrefix, dest[extra + i]);
        const auto type = EntryTypeOf(io, destPrefix);
        if (type && *type != DirectoryEntryType::Dir) {
            context.Error(q(destPrefix) + " exists but is not a directory");
            return false;
        }
        if (!type) {
            if (io.CreateDirectory(destPrefix, kDirMode) != 0) {
                context.Error("cannot create directory " + q(destPrefix)
                    + ": " + CreateFailedReason(io, destPrefix));
                return false;
            }
            if (verbose) {
                context.Out(sourcePrefix + " -> " + destPrefix + "\n");
            }
        }
    }
    return true;
}

// The attribute words of --preserve/--no-preserve, in GNU's order. Values:
// 1 timestamps, 2 all (both set preserveTimes), 3 context and 4 xattr (not
// treated), 5 ownership and 6 links (nothing to preserve in HaisosOS); 0 mode
// too. Each value is its own, so the "Valid arguments are:" block GNU prints
// on a bad word shows one name a line, as GNU does.
std::optional<int> MatchPreserveWord(BuiltinContext& context, const std::string& longOption,
                                      const std::string& word) {
    static const std::vector<ArgChoice> kAttributes = {
        {"mode", 0},
        {"timestamps", 1},
        {"ownership", 5},
        {"links", 6},
        {"context", 3},
        {"xattr", 4},
        {"all", 2},
    };
    return ArgMatch(context, longOption, word, kAttributes);
}

// One comma-separated --preserve/--no-preserve list. Returns false on a word
// GNU would refuse too (its block is then printed); |preserveTimes| is set for
// timestamps/all and cleared by --no-preserve's.
bool ApplyPreserveList(BuiltinContext& context, const std::string& longOption,
                       const std::string& list, bool preserve, bool& preserveTimes) {
    size_t start = 0;
    while (start <= list.size()) {
        size_t comma = list.find(',', start);
        const std::string word = list.substr(start, comma == std::string::npos
            ? std::string::npos : comma - start);
        if (!word.empty()) {
            const auto matched = MatchPreserveWord(context, longOption, word);
            if (!matched) {
                return false;
            }
            if (preserve) {
                if (*matched == 1 || *matched == 2) {
                    preserveTimes = true;
                }
                if (*matched == 3 || *matched == 4) {
                    context.NotTreated("--preserve=" + word);
                }
            } else {
                if (*matched == 1 || *matched == 2) {
                    preserveTimes = false;
                }
            }
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return true;
}

class CpCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "cp"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'a', "archive", kOptionArchive, BuiltinArgument::None, "", "same as -dR --preserve=all"},
            {0, "attributes-only", kOptionAttributesOnly, BuiltinArgument::None, "", "don't copy the file data, just the attributes"},
            {0, "backup", kOptionBackup, BuiltinArgument::Optional, "CONTROL", "make a backup of each existing destination file (none, simple, existing or numbered)"},
            {'b', "", kOptionBackupShort, BuiltinArgument::None, "", "like --backup but does not accept an argument"},
            {0, "copy-contents", kBuiltinNotTreated},
            {'d', "", kOptionNoDereference, BuiltinArgument::None, "", "no effect: HaisosOS has no links"},
            {0, "debug", kBuiltinNotTreated},
            {'f', "force", kOptionForce, BuiltinArgument::None, "", "if an existing destination file cannot be opened, remove it and try again"},
            {'i', "interactive", kOptionInteractive, BuiltinArgument::None, "", "prompt before overwrite (overrides a previous -n)"},
            {'H', "", kOptionFollowCommandLine, BuiltinArgument::None, "", "no effect: HaisosOS has no links"},
            {'l', "link", kOptionLink, BuiltinArgument::None, "", "fails: HaisosOS creates no links"},
            {'L', "dereference", kOptionDereference, BuiltinArgument::None, "", "no effect: HaisosOS has no links"},
            {'n', "no-clobber", kOptionNoClobber, BuiltinArgument::None, "", "do not overwrite an existing file, and do not fail"},
            {'P', "no-dereference", kOptionNeverDereference, BuiltinArgument::None, "", "no effect: HaisosOS has no links"},
            {'p', "", kOptionPreserve, BuiltinArgument::None, "", "preserve modification times"},
            {0, "preserve", kOptionPreserveList, BuiltinArgument::Optional, "ATTR_LIST", "preserve the given attributes; only timestamps have an effect"},
            {0, "no-preserve", kOptionNoPreserve, BuiltinArgument::Required, "ATTR_LIST", "don't preserve the given attributes"},
            {0, "parents", kOptionParents, BuiltinArgument::None, "", "use the full source file name under DIRECTORY"},
            {'R', "", kOptionRecursive, BuiltinArgument::None, "", "same as -r"},
            {'r', "recursive", kOptionRecursive, BuiltinArgument::None, "", "copy directories recursively"},
            {0, "reflink", kBuiltinNotTreated, BuiltinArgument::Optional, "WHEN"},
            {0, "remove-destination", kOptionRemoveDestination, BuiltinArgument::None, "", "remove each existing destination file before opening it"},
            {0, "sparse", kBuiltinNotTreated, BuiltinArgument::Required, "WHEN"},
            {0, "strip-trailing-slashes", kOptionStripTrailingSlashes, BuiltinArgument::None, "", "remove any trailing slashes from each SOURCE"},
            {'s', "symbolic-link", kOptionSymbolicLink, BuiltinArgument::None, "", "fails: HaisosOS creates no links"},
            {'S', "suffix", kOptionSuffix, BuiltinArgument::Required, "SUFFIX", "override the usual backup suffix"},
            {'t', "target-directory", kOptionTargetDirectory, BuiltinArgument::Required, "DIRECTORY", "copy all SOURCE arguments into DIRECTORY"},
            {'T', "no-target-directory", kOptionNoTargetDirectory, BuiltinArgument::None, "", "treat DEST as a normal file"},
            {0, "update", kOptionUpdate, BuiltinArgument::Optional, "UPDATE", "replace only older files: all, none or older"},
            {'u', "", kOptionUpdateShort, BuiltinArgument::None, "", "equivalent to --update[=older]"},
            {'v', "verbose", kOptionVerbose, BuiltinArgument::None, "", "explain what is being done"},
            {'x', "one-file-system", kBuiltinNotTreated},
            {'Z', "", kBuiltinNotTreated},
            {0, "context", kBuiltinNotTreated, BuiltinArgument::Optional, "CTX"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "copy files and directories",
            {
                "cp [OPTION]... [-T] SOURCE DEST",
                "cp [OPTION]... SOURCE... DIRECTORY",
                "cp [OPTION]... -t DIRECTORY SOURCE...",
            },
            "-l and -s fail: HaisosOS creates no links.\n"
            "-d, -H, -L and -P change nothing: there are no links.\n"
            "--preserve keeps timestamps only (no modes, owners or links).\n"
            "Entries of a directory are copied in name order, not the disk's.\n"
            "A directory copied into itself is refused before anything is copied."};
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }

        CopyOptions options;
        std::optional<std::string> targetDirectory;
        bool noTargetDirectory = false;
        bool parents = false;
        bool stripTrailingSlashes = false;
        bool link = false;
        bool symbolicLink = false;
        bool backupOn = false;
        bool noClobbered = false;   // -n set options.update to None
        BackupMode backupMode = BackupMode::Existing;
        std::string backupSuffix;

        for (const auto& option : parsed->options) {
            switch (option.id) {
                case kOptionArchive:
                    options.recursive = true;
                    options.preserveTimes = true;
                    break;
                case kOptionAttributesOnly: options.attributesOnly = true; break;
                case kOptionBackup: {
                    backupOn = true;
                    if (option.hasArgument) {
                        if (!ParseBackupControl(context, option.argument, backupMode)) {
                            return 1;
                        }
                    } else if (!BackupModeFromEnvironment(context, backupMode)) {
                        return 1;
                    }
                    break;
                }
                case kOptionBackupShort:
                    backupOn = true;
                    if (!BackupModeFromEnvironment(context, backupMode)) {
                        return 1;
                    }
                    break;
                case kOptionNoDereference:
                case kOptionFollowCommandLine:
                case kOptionDereference:
                case kOptionNeverDereference:
                    break;  // no links: nothing to dereference or preserve
                case kOptionForce: options.force = true; break;
                case kOptionInteractive:
                    options.interactive = true;
                    if (noClobbered) {
                        options.update = UpdateMode::All;
                    }
                    break;
                case kOptionLink: link = true; break;
                case kOptionNoClobber:
                    context.Error("warning: behavior of -n is non-portable and may change in future; use --update=none instead");
                    options.update = UpdateMode::None;
                    options.interactive = false;
                    noClobbered = true;
                    break;
                case kOptionPreserve: options.preserveTimes = true; break;
                case kOptionPreserveList: {
                    if (!option.hasArgument) {
                        // mode, ownership, timestamps: only timestamps are
                        // anything HaisosOS can keep.
                        options.preserveTimes = true;
                        break;
                    }
                    if (!ApplyPreserveList(context, "--preserve", option.argument, true, options.preserveTimes)) {
                        return 1;
                    }
                    break;
                }
                case kOptionNoPreserve:
                    if (!ApplyPreserveList(context, "--no-preserve", option.argument, false, options.preserveTimes)) {
                        return 1;
                    }
                    break;
                case kOptionParents: parents = true; break;
                case kOptionRecursive: options.recursive = true; break;
                case kOptionRemoveDestination: options.removeDestination = true; break;
                case kOptionStripTrailingSlashes: stripTrailingSlashes = true; break;
                case kOptionSymbolicLink: symbolicLink = true; break;
                case kOptionSuffix:
                    backupOn = true;  // a suffix alone backs up, as GNU 9.4
                    backupSuffix = option.argument;
                    break;
                case kOptionTargetDirectory: targetDirectory = option.argument; break;
                case kOptionNoTargetDirectory: noTargetDirectory = true; break;
                case kOptionUpdate: {
                    if (!option.hasArgument) {
                        options.update = UpdateMode::Older;
                        break;
                    }
                    static const std::vector<ArgChoice> kUpdates = {
                        {"all", static_cast<int>(UpdateMode::All)},
                        {"none", static_cast<int>(UpdateMode::None)},
                        {"older", static_cast<int>(UpdateMode::Older)},
                    };
                    const auto matched = ArgMatch(context, "--update", option.argument, kUpdates);
                    if (!matched) {
                        return 1;
                    }
                    options.update = static_cast<UpdateMode>(*matched);
                    break;
                }
                case kOptionUpdateShort: options.update = UpdateMode::Older; break;
                case kOptionVerbose: options.verbose = CopyVerbose::Cp; break;
                default: break;  // not treated (already reported)
            }
        }

        if (backupOn) {
            options.backup = backupMode;
            if (backupSuffix.empty()) {
                backupSuffix = "~";
                if (auto env = context.Process().GetEnvironment()->GetVariable("SIMPLE_BACKUP_SUFFIX")) {
                    backupSuffix = *env;
                }
            }
            options.backupSuffix = backupSuffix;
        }

        const auto targets = ResolveCopyTargets(context, parsed->operands, targetDirectory,
                                                noTargetDirectory, parents, stripTrailingSlashes);
        if (!targets) {
            return 1;
        }

        // -l and -s: no link may ever be created, so every target fails and
        // nothing is copied.
        if (link || symbolicLink) {
            return RunLinks(context, *targets, symbolicLink, options.recursive);
        }

        BuiltinPrompt prompt(context);
        int status = 0;
        for (const auto& target : *targets) {
            if (context.StopRequested()) {
                return 1;
            }
            if (parents && !MakeParents(context, target, options.verbose == CopyVerbose::Cp)) {
                status = 1;
                continue;
            }
            if (!CopyPath(context, &prompt, target.source, target.dest, options)) {
                status = 1;
            }
        }
        return status;
    }

private:
    // --backup/-b without a word: the mode from the environment's
    // VERSION_CONTROL, else existing. |mode| keeps its value on an absent
    // variable.
    static bool BackupModeFromEnvironment(BuiltinContext& context, BackupMode& mode) {
        auto env = context.Process().GetEnvironment()->GetVariable("VERSION_CONTROL");
        if (!env) {
            mode = BackupMode::Existing;
            return true;
        }
        return ParseBackupControl(context, *env, mode);
    }

    static int RunLinks(BuiltinContext& context, const std::vector<CopyTarget>& targets,
                        bool symbolicLink, bool recursive) {
        IFileIO& io = context.IO();
        int status = 0;
        for (const auto& target : targets) {
            FileStatus sourceStatus;
            if (io.Stat(target.source, sourceStatus) != 0) {
                context.Error("cannot stat " + q(target.source)
                    + ": " + StatMissingReasonOf(io, target.source));
                status = 1;
                continue;
            }
            if (sourceStatus.type == DirectoryEntryType::Dir && !recursive) {
                context.Error("-r not specified; omitting directory " + q(target.source));
                status = 1;
                continue;
            }
            context.Error(std::string("cannot create ")
                + (symbolicLink ? "symbolic link " : "hard link ") + q(target.dest)
                + " to " + q(target.source) + ": Operation not permitted");
            status = 1;
        }
        return status;
    }

    // BuiltinCopy's rule for a failed Stat.
    static std::string StatMissingReasonOf(IFileIO& io, const std::string& path) {
        std::string dir = VirtualParentOf(io.ResolvePath(path));
        while (true) {
            if (auto type = EntryTypeOf(io, dir)) {
                return *type == DirectoryEntryType::Dir
                    ? "No such file or directory"
                    : "Not a directory";
            }
            if (dir == "/") {
                return "No such file or directory";
            }
            dir = VirtualParentOf(dir);
        }
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateCpCommand() {
    return std::make_shared<CpCommand>();
}

} // namespace Haisos