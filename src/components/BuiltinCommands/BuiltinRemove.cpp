#include "BuiltinRemove.h"
#include <algorithm>
#include <vector>
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {

namespace {

// GNU's quoteaf: every name in a removal message, quoted even when plain.
std::string q(const std::string& name) {
    return ShellEscapeQuoted(name, /*always=*/true);
}

// The last segment of |path| as written, trailing slashes stripped.
std::string LastSegmentOf(const std::string& path) {
    std::string trimmed = path;
    while (trimmed.size() > 1 && trimmed.back() == '/') {
        trimmed.pop_back();
    }
    return VirtualLastSegment(trimmed);
}

// |path| without trailing slashes: what a child of it is built from, so
// "d/" and "d" both give "d/name".
std::string WithoutTrailingSlashes(const std::string& path) {
    std::string trimmed = path;
    while (trimmed.size() > 1 && trimmed.back() == '/') {
        trimmed.pop_back();
    }
    return trimmed;
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

// Why a RemoveDirectory failed, since IFileIO gives no reason: a directory
// that still has entries is plain, and everything else -- a mount point, the
// root of a filesystem, a read-only filesystem too -- is EBUSY.
std::string DirectoryRemovalReason(IFileIO& io, const std::string& path) {
    return HasEntries(io, path) ? "Directory not empty" : "Device or resource busy";
}

// The whole question GNU asks about a file, ending in "? ": its name prefix,
// "remove", the file-type wording ("regular file", "regular empty file",
// "character special file") and the quoted path.
std::string FileQuestion(BuiltinContext& context, const FileStatus& status) {
    std::string what = "regular file";
    if (status.type == DirectoryEntryType::CharDevice) {
        what = "character special file";
    } else if (status.size == 0) {
        what = "regular empty file";
    }
    return context.Name() + ": remove " + what;
}

// Removes an empty directory, asking first when interactive. Returns false
// only on a failure (a declined prompt is no failure).
bool RemoveEmptyDirectory(BuiltinContext& context, BuiltinPrompt* prompt,
                          const std::string& path, const std::string& shown,
                          const RemoveOptions& options) {
    if (options.interactive && prompt) {
        if (!prompt->Ask(context.Name() + ": remove directory " + shown + "? ")) {
            return true;
        }
    }
    IFileIO& io = context.IO();
    if (io.RemoveDirectory(path) != 0) {
        context.Error("cannot remove " + shown + ": " + DirectoryRemovalReason(io, path));
        return false;
    }
    if (options.verbose) {
        context.Out("removed directory " + shown + "\n");
    }
    return true;
}

} // namespace

bool RemoveOperand(BuiltinContext& context, BuiltinPrompt* prompt,
                   const std::string& path, const RemoveOptions& options) {
    IFileIO& io = context.IO();
    const std::string shown = q(path);

    FileStatus status;
    if (io.Stat(path, status) != 0) {
        if (options.ignoreMissing) {
            return true;
        }
        context.Error("cannot remove " + shown + ": No such file or directory");
        return false;
    }
    // A trailing '/' names a directory: on anything else the system answers
    // ENOTDIR, before any question is asked.
    if (path.size() > 1 && path.back() == '/' && status.type != DirectoryEntryType::Dir) {
        context.Error("cannot remove " + shown + ": Not a directory");
        return false;
    }
    // "." and ".." are never removed, with -r or -d.
    const std::string lastSegment = LastSegmentOf(path);
    if ((lastSegment == "." || lastSegment == "..") && (options.recursive || options.emptyDirectories)) {
        context.Error("refusing to remove '.' or '..' directory: skipping " + shown);
        return false;
    }

    if (status.type != DirectoryEntryType::Dir) {
        if (options.interactive && prompt) {
            if (!prompt->Ask(FileQuestion(context, status) + " " + shown + "? ")) {
                return true;
            }
        }
        if (io.RemoveFile(path) != 0) {
            // A builtin's path, a read-only filesystem: no reason is known,
            // and both are EACCES.
            context.Error("cannot remove " + shown + ": Permission denied");
            return false;
        }
        if (options.verbose) {
            context.Out("removed " + shown + "\n");
        }
        return true;
    }

    if (!options.recursive) {
        if (!options.emptyDirectories) {
            context.Error("cannot remove " + shown + ": Is a directory");
            return false;
        }
        return RemoveEmptyDirectory(context, prompt, path, shown, options);
    }

    if (options.preserveRoot && io.ResolvePath(path) == "/") {
        context.Error("it is dangerous to operate recursively on '/'");
        context.Error("use --no-preserve-root to override this failsafe");
        return false;
    }

    // Recursive, post-order: the entries first, then the directory itself.
    std::vector<std::string> names;
    for (const auto& entry : io.ReadDirectory(path)) {
        if (entry.name != "." && entry.name != "..") {
            names.push_back(entry.name);
        }
    }
    if (options.interactive && prompt && !names.empty()) {
        if (!prompt->Ask(context.Name() + ": descend into directory " + shown + "? ")) {
            return true;
        }
    }
    // Sorted by name, byte order -- a Haisos choice: GNU follows the disk's
    // order, which no listing guarantees.
    std::sort(names.begin(), names.end());
    const std::string base = WithoutTrailingSlashes(path);
    bool anyFailed = false;
    for (const auto& name : names) {
        if (context.StopRequested()) {
            return false;
        }
        if (!RemoveOperand(context, prompt, base + "/" + name, options)) {
            anyFailed = true;
        }
    }
    if (anyFailed) {
        // The directory holding something that failed is left alone, without
        // a question or a message of its own (GNU marks the ancestors of a
        // failure).
        return false;
    }
    return RemoveEmptyDirectory(context, prompt, path, shown, options);
}

} // namespace Haisos