#include "BuiltinCopy.h"
#include <algorithm>
#include <cstdlib>
#include <sys/stat.h>
#include "BuiltinPrompt.h"
#include "BuiltinText.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/Filesystem/VirtualPath.h"

namespace Haisos {
namespace {

#ifdef _WIN32
constexpr int kDirectoryMode = _S_IREAD | _S_IWRITE;
#else
constexpr int kDirectoryMode = S_IRWXU | S_IRWXG | S_IRWXO;
#endif

// GNU's quoteaf: every name in a copy message, quoted even when plain.
std::string q(const std::string& name) {
    return ShellEscapeQuoted(name, /*always=*/true);
}

// |path| without trailing slashes: what a child of it is built from, so
// "d/" and "d" both give "d/name". A lone "/" keeps its slash.
std::string WithoutTrailingSlashes(const std::string& path) {
    std::string trimmed = path;
    while (trimmed.size() > 1 && trimmed.back() == '/') {
        trimmed.pop_back();
    }
    return trimmed;
}

// dir + "/" + name, with no doubled slash ("/" + "x" is "/x").
std::string JoinPath(const std::string& dir, const std::string& name) {
    std::string joined = dir;
    if (!joined.empty() && joined.back() != '/') {
        joined += '/';
    }
    return joined + name;
}

// Why Stat(path) found nothing: "Not a directory" when a segment on the way is
// a file, else "No such file or directory".
std::string StatMissingReason(IFileIO& io, const std::string& path) {
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

// Why creating |path| failed: "No such file or directory" when its directory is
// missing, "Not a directory" when that is a file, else -- a builtin's path, a
// read-only filesystem -- "Permission denied" (IFileIO gives no reason).
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

// The path |source|'s copy gets under |dir|: its last name, or with --parents
// its whole path (a leading '/' dropped), as GNU builds the destination.
std::string DestUnderDirectory(const std::string& source, const std::string& dir, bool parents) {
    const std::string trimmed = WithoutTrailingSlashes(source);
    if (parents) {
        std::string path = trimmed;
        if (!path.empty() && path[0] == '/') {
            path.erase(0, 1);
        }
        return JoinPath(dir, path);
    }
    return JoinPath(dir, VirtualLastSegment(trimmed));
}

// cp -v, a copied file or a created directory: 'a' -> 'b', with the backup
// named. mv (across filesystems) prints its own lines instead.
void WriteVerboseFile(BuiltinContext& context, const std::string& source,
                      const std::string& dest, const std::string& backupPath,
                      const CopyOptions& options) {
    if (options.verbose == CopyVerbose::None) {
        return;
    }
    if (options.verbose == CopyVerbose::Mv) {
        context.Out("copied " + q(source) + " -> " + q(dest) + "\n");
        return;
    }
    std::string line = q(source) + " -> " + q(dest);
    if (!backupPath.empty()) {
        line += " (backup: " + q(backupPath) + ")";
    }
    context.Out(line + "\n");
}

void WriteVerboseDirectory(BuiltinContext& context, const std::string& source,
                           const std::string& dest, const CopyOptions& options) {
    if (options.verbose == CopyVerbose::None) {
        return;
    }
    if (options.verbose == CopyVerbose::Mv) {
        context.Out("created directory " + q(dest) + "\n");
        return;
    }
    context.Out(q(source) + " -> " + q(dest) + "\n");
}

bool CopyFile(BuiltinContext& context, BuiltinPrompt* prompt, const std::string& source,
              const std::string& dest, const FileStatus& sourceStatus,
              const FileStatus& destStatus, bool destExists, const CopyOptions& options);

bool CopyDirectory(BuiltinContext& context, BuiltinPrompt* prompt, const std::string& source,
                   const std::string& dest, const FileStatus& sourceStatus,
                   bool destExists, const CopyOptions& options);

} // namespace

std::string BackupPathFor(BuiltinContext& context, const std::string& dest,
                          BackupMode mode, const std::string& suffix) {
    if (mode == BackupMode::Simple) {
        return dest + suffix;
    }
    IFileIO& io = context.IO();
    const std::string absolute = io.ResolvePath(WithoutTrailingSlashes(dest));
    const std::string name = VirtualLastSegment(absolute);
    // The largest N of the <name>.~N~ entries in dest's directory.
    long largest = 0;
    for (const auto& entry : io.ReadDirectory(VirtualParentOf(absolute))) {
        const std::string prefix = name + ".~";
        if (entry.name.compare(0, prefix.size(), prefix) != 0 ||
            entry.name.size() < prefix.size() + 2 ||
            entry.name.back() != '~') {
            continue;
        }
        const std::string number = entry.name.substr(prefix.size(), entry.name.size() - prefix.size() - 1);
        if (number.empty() ||
            number.find_first_not_of("0123456789") != std::string::npos) {
            continue;
        }
        largest = std::max(largest, atol(number.c_str()));
    }
    if (mode == BackupMode::Existing && largest == 0) {
        return dest + suffix;
    }
    return dest + ".~" + std::to_string(largest + 1) + "~";
}

bool ParseBackupControl(BuiltinContext& context, const std::string& word, BackupMode& out) {
    static const std::vector<ArgChoice> kControls = {
        {"none", static_cast<int>(BackupMode::None)},
        {"off", static_cast<int>(BackupMode::None)},
        {"simple", static_cast<int>(BackupMode::Simple)},
        {"never", static_cast<int>(BackupMode::Simple)},
        {"existing", static_cast<int>(BackupMode::Existing)},
        {"nil", static_cast<int>(BackupMode::Existing)},
        {"numbered", static_cast<int>(BackupMode::Numbered)},
        {"t", static_cast<int>(BackupMode::Numbered)},
    };
    const auto matched = ArgMatch(context, "backup type", word, kControls);
    if (!matched) {
        return false;
    }
    out = static_cast<BackupMode>(*matched);
    return true;
}

std::optional<std::vector<CopyTarget>> ResolveCopyTargets(
    BuiltinContext& context, std::vector<std::string> operands,
    const std::optional<std::string>& targetDirectory, bool noTargetDirectory,
    bool parents, bool stripTrailingSlashes) {
    IFileIO& io = context.IO();

    if (operands.empty()) {
        context.Error("missing file operand");
        context.TryHelp();
        return std::nullopt;
    }
    if (targetDirectory && noTargetDirectory) {
        context.Error("cannot combine --target-directory (-t) and --no-target-directory (-T)");
        return std::nullopt;
    }

    // -t DIR: every operand is a source copied under DIR.
    if (targetDirectory) {
        const std::string& dir = *targetDirectory;
        const auto type = EntryTypeOf(io, dir);
        if (!type) {
            context.Error("target directory " + q(dir) + ": No such file or directory");
            return std::nullopt;
        }
        if (*type != DirectoryEntryType::Dir) {
            context.Error("target directory " + q(dir) + ": Not a directory");
            return std::nullopt;
        }
        std::vector<CopyTarget> targets;
        for (auto& source : operands) {
            if (stripTrailingSlashes) {
                source = WithoutTrailingSlashes(source);
            }
            targets.push_back({source, DestUnderDirectory(source, dir, parents)});
        }
        return targets;
    }

    // -T: exactly two operands, the second the destination as is.
    if (noTargetDirectory) {
        if (operands.size() < 2) {
            context.Error("missing destination file operand after " + q(operands[0]));
            context.TryHelp();
            return std::nullopt;
        }
        if (operands.size() > 2) {
            context.Error("extra operand " + q(operands[2]));
            context.TryHelp();
            return std::nullopt;
        }
        if (parents) {
            context.Error("with --parents, the destination must be a directory");
            context.TryHelp();
            return std::nullopt;
        }
        return std::vector<CopyTarget>{{operands[0], operands[1]}};
    }

    // Otherwise the last operand is the destination.
    const std::string target = operands.back();
    operands.pop_back();
    if (operands.empty()) {
        context.Error("missing destination file operand after " + q(target));
        context.TryHelp();
        return std::nullopt;
    }

    // With more than one source the destination must be an existing directory;
    // with one it is used as a directory only when it is one.
    bool targetIsDirectory;
    const auto targetType = EntryTypeOf(io, target);
    if (operands.size() > 1) {
        if (!targetType) {
            context.Error("target " + q(target) + ": No such file or directory");
            return std::nullopt;
        }
        if (*targetType != DirectoryEntryType::Dir) {
            context.Error("target " + q(target) + ": Not a directory");
            return std::nullopt;
        }
        targetIsDirectory = true;
    } else {
        targetIsDirectory = targetType == DirectoryEntryType::Dir;
    }

    if (parents && !targetIsDirectory) {
        context.Error("with --parents, the destination must be a directory");
        context.TryHelp();
        return std::nullopt;
    }

    if (!targetIsDirectory) {
        return std::vector<CopyTarget>{{operands[0], target}};
    }
    std::vector<CopyTarget> targets;
    for (auto& source : operands) {
        if (stripTrailingSlashes) {
            source = WithoutTrailingSlashes(source);
        }
        targets.push_back({source, DestUnderDirectory(source, target, parents)});
    }
    return targets;
}

bool CopyPath(BuiltinContext& context, BuiltinPrompt* prompt,
              const std::string& source, const std::string& dest,
              const CopyOptions& options) {
    IFileIO& io = context.IO();

    FileStatus sourceStatus;
    if (io.Stat(source, sourceStatus) != 0) {
        context.Error("cannot stat " + q(source) + ": " + StatMissingReason(io, source));
        return false;
    }

    if (sourceStatus.type == DirectoryEntryType::Dir && !options.recursive) {
        context.Error("-r not specified; omitting directory " + q(source));
        return false;
    }

    const std::string resolvedSource = io.ResolvePath(source);
    const std::string resolvedDest = io.ResolvePath(dest);
    if (resolvedSource == resolvedDest) {
        context.Error(q(source) + " and " + q(dest) + " are the same file");
        return false;
    }

    // A directory copied into (or below) itself would never end.
    if (sourceStatus.type == DirectoryEntryType::Dir) {
        const std::string below = resolvedSource == "/" ? "/" : resolvedSource + "/";
        if (resolvedDest.size() > below.size() &&
            resolvedDest.compare(0, below.size(), below) == 0) {
            context.Error("cannot copy a directory, " + q(source) + ", into itself, " + q(dest));
            return false;
        }
    }

    FileStatus destStatus;
    const bool destExists = io.Stat(dest, destStatus) == 0;
    if (destExists) {
        if (sourceStatus.type == DirectoryEntryType::Dir && destStatus.type != DirectoryEntryType::Dir) {
            context.Error("cannot overwrite non-directory " + q(dest) + " with directory " + q(source));
            return false;
        }
        if (sourceStatus.type != DirectoryEntryType::Dir && destStatus.type == DirectoryEntryType::Dir) {
            context.Error("cannot overwrite directory " + q(dest) + " with non-directory");
            return false;
        }
    }

    // A device copied recursively would have to be created as a device, which
    // Haisos cannot; as a plain operand its contents are copied, as GNU does.
    if (sourceStatus.type == DirectoryEntryType::CharDevice && options.recursive) {
        context.Error("cannot create special file " + q(dest) + ": Operation not permitted");
        return false;
    }

    if (sourceStatus.type == DirectoryEntryType::Dir) {
        return CopyDirectory(context, prompt, source, dest, sourceStatus, destExists, options);
    }
    return CopyFile(context, prompt, source, dest, sourceStatus, destStatus, destExists, options);
}

namespace {

bool CopyFile(BuiltinContext& context, BuiltinPrompt* prompt, const std::string& source,
              const std::string& dest, const FileStatus& sourceStatus,
              const FileStatus& destStatus, bool destExists, const CopyOptions& options) {
    IFileIO& io = context.IO();

    // A trailing '/' names a directory; nothing else can be copied onto it.
    if (dest.size() > 1 && dest.back() == '/') {
        context.Error("cannot create regular file " + q(dest) + ": Not a directory");
        return false;
    }

    // -n and -u: an existing destination that would not be replaced is
    // skipped -- not a failure.
    if (destExists) {
        if (options.update == UpdateMode::None) {
            return true;
        }
        if (options.update == UpdateMode::Older &&
            destStatus.modificationTime >= sourceStatus.modificationTime) {
            return true;
        }
        if (options.interactive && prompt) {
            if (!prompt->Ask(context.Name() + ": overwrite " + q(dest) + "? ")) {
                return false;
            }
        }
    }

    // The backup: the old destination is renamed aside before the copy.
    std::string backupPath;
    if (destExists && options.backup != BackupMode::None) {
        backupPath = BackupPathFor(context, dest, options.backup, options.backupSuffix);
        if (io.Rename(dest, backupPath) != 0) {
            context.Error("cannot backup " + q(dest) + ": Permission denied");
            return false;
        }
    }

    if (options.removeDestination && EntryTypeOf(io, dest)) {
        if (io.RemoveFile(dest) == 0 && options.verbose == CopyVerbose::Cp) {
            context.Out("removed " + q(dest) + "\n");
        }
    }

    auto in = io.OpenFile(source, kFileOpenReadOnly);
    if (!in) {
        context.Error("cannot open " + q(source) + " for reading: Permission denied");
        return false;
    }

    const int flags = options.attributesOnly
        ? kFileOpenWriteCreateAppend   // GNU --attributes-only does not truncate
        : kFileOpenWriteCreateTruncate;
    auto out = io.OpenFile(dest, flags, kFileCreateMode);
    if (!out && destExists && options.force) {
        // -f: a destination that cannot be opened is removed and tried again.
        io.RemoveFile(dest);
        out = io.OpenFile(dest, flags, kFileCreateMode);
    }
    if (!out) {
        context.Error("cannot create regular file " + q(dest) + ": " + CreateFailedReason(io, dest));
        return false;
    }

    if (!options.attributesOnly) {
        char buffer[64 * 1024];
        while (true) {
            // A copy cut short by a stop is not a copy: mv must not then
            // remove its source.
            if (context.StopRequested()) {
                return false;
            }
            const ssize_t n = in->Read(buffer, sizeof(buffer));
            if (n < 0) {
                context.Error("error reading " + q(source));
                return false;
            }
            if (n == 0) {
                break;
            }
            if (WriteFully(*out, std::string_view(buffer, static_cast<size_t>(n))) < 0) {
                context.Error("error writing " + q(dest));
                return false;
            }
        }
    }
    in.reset();
    out.reset();

    if (options.preserveTimes &&
        io.SetTimes(dest, sourceStatus.accessTime, sourceStatus.modificationTime) != 0) {
        context.Error("preserving times for " + q(dest) + ": Permission denied");
        return false;
    }

    WriteVerboseFile(context, source, dest, backupPath, options);
    return true;
}

bool CopyDirectory(BuiltinContext& context, BuiltinPrompt* prompt, const std::string& source,
                   const std::string& dest, const FileStatus& sourceStatus,
                   bool destExists, const CopyOptions& options) {
    IFileIO& io = context.IO();

    // A missing directory is created; an existing one is merged into, with no
    // line of its own.
    if (!destExists) {
        if (io.CreateDirectory(dest, kDirectoryMode) != 0) {
            context.Error("cannot create directory " + q(dest) + ": " + CreateFailedReason(io, dest));
            return false;
        }
        WriteVerboseDirectory(context, source, dest, options);
    }

    // Sorted by name, byte order -- a Haisos choice: GNU follows the disk's
    // order, which no listing guarantees.
    std::vector<std::string> names;
    for (const auto& entry : io.ReadDirectory(source)) {
        if (entry.name != "." && entry.name != "..") {
            names.push_back(entry.name);
        }
    }
    std::sort(names.begin(), names.end());
    bool ok = true;
    for (const auto& name : names) {
        if (context.StopRequested()) {
            return false;
        }
        if (!CopyPath(context, prompt, JoinPath(source, name), JoinPath(dest, name), options)) {
            ok = false;
        }
    }

    // After the contents: copying them changes the directory's times again.
    if (options.preserveTimes &&
        io.SetTimes(dest, sourceStatus.accessTime, sourceStatus.modificationTime) != 0) {
        context.Error("preserving times for " + q(dest) + ": Permission denied");
        ok = false;
    }
    return ok;
}

} // namespace

} // namespace Haisos