#include "HaisosFileOperations.h"
#include <algorithm>
#include <optional>
#include <sstream>
#include <system_error>
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/Filesystem/VirtualPath.h"
#include "src/components/Logger/Logger.h"

namespace Haisos {

namespace {

#ifdef _WIN32
constexpr int kDirectoryCreateMode = _S_IREAD | _S_IWRITE;
#else
constexpr int kDirectoryCreateMode = S_IRWXU;
#endif

constexpr size_t kCopyChunkSize = 64 * 1024;

const char* DirectiveName(HaisosFileOperationType type) {
    switch (type) {
        case HaisosFileOperationType::Create: return "CREATE";
        case HaisosFileOperationType::Append: return "APPEND";
        case HaisosFileOperationType::CreateDir: return "CREATE_DIR";
        case HaisosFileOperationType::Builtin: return "BUILTIN";
        case HaisosFileOperationType::Copy: return "COPY";
        case HaisosFileOperationType::Delete: return "DELETE";
        case HaisosFileOperationType::OutCopy: return "OUTCOPY";
    }
    return "?";
}

// Creates normalizedDirectory and every missing directory above it; any that
// already exist are left as they are.
bool EnsureDirectories(IFileSystem& fs, const std::string& normalizedDirectory, std::string& outReason) {
    std::istringstream segments(normalizedDirectory);
    std::string segment;
    std::string current;
    while (std::getline(segments, segment, '/')) {
        if (segment.empty()) {
            continue;
        }
        current += "/" + segment;
        auto type = EntryTypeOf(fs, current);
        if (!type) {
            if (fs.CreateDirectory(current, kDirectoryCreateMode) != 0) {
                outReason = "cannot create directory " + current;
                return false;
            }
        } else if (*type != DirectoryEntryType::Dir) {
            outReason = current + " is a file, not a directory";
            return false;
        }
    }
    return true;
}

bool EnsureParentDirectories(IFileSystem& fs, const std::string& normalizedPath, std::string& outReason) {
    return EnsureDirectories(fs, VirtualParentOf(normalizedPath), outReason);
}

bool WriteAll(IFileSystem& fs, int fd, const char* data, size_t size) {
    size_t written = 0;
    while (written < size) {
        ssize_t n = fs.WriteFile(fd, data + written, size - written);
        if (n <= 0) {
            return false;
        }
        written += static_cast<size_t>(n);
    }
    return true;
}

bool WriteContent(IFileSystem& fs, const std::string& normalizedPath, const std::string& content, bool append, std::string& outReason) {
    if (EntryTypeOf(fs, normalizedPath) == std::optional<char>(DirectoryEntryType::Dir)) {
        outReason = "it is a directory";
        return false;
    }
    if (!EnsureParentDirectories(fs, normalizedPath, outReason)) {
        return false;
    }
    int fd = fs.OpenFile(normalizedPath, append ? kFileOpenWriteCreateAppend : kFileOpenWriteCreateTruncate, kFileCreateMode);
    if (fd < 0) {
        outReason = "cannot open it for writing";
        return false;
    }
    const bool ok = WriteAll(fs, fd, content.data(), content.size());
    const bool closed = fs.CloseFile(fd) == 0;
    if (!ok || !closed) {
        outReason = "writing to it failed";
        return false;
    }
    return true;
}

// Streams one file from one filesystem to another. No size cap, unlike
// ReadWholeFile, whose silent truncation would be a silently corrupt copy.
bool CopyFileBetween(IFileSystem& from, const std::string& fromPath, IFileSystem& to, const std::string& toPath, std::string& outReason) {
    auto sourceType = EntryTypeOf(from, fromPath);
    if (!sourceType) {
        outReason = "the source does not exist";
        return false;
    }
    if (*sourceType != DirectoryEntryType::File) {
        outReason = *sourceType == DirectoryEntryType::Dir
            ? "the source is a directory; only files can be copied"
            : "the source is a device; only files can be copied";
        return false;
    }
    if (EntryTypeOf(to, toPath) == std::optional<char>(DirectoryEntryType::Dir)) {
        outReason = "the destination is a directory";
        return false;
    }
    if (!EnsureParentDirectories(to, toPath, outReason)) {
        return false;
    }

    int in = from.OpenFile(fromPath, kFileOpenReadOnly);
    if (in < 0) {
        outReason = "cannot open the source for reading";
        return false;
    }
    int out = to.OpenFile(toPath, kFileOpenWriteCreateTruncate, kFileCreateMode);
    if (out < 0) {
        from.CloseFile(in);
        outReason = "cannot open the destination for writing";
        return false;
    }

    std::vector<char> buffer(kCopyChunkSize);
    bool ok = true;
    while (true) {
        ssize_t n = from.ReadFile(in, buffer.data(), buffer.size());
        if (n == 0) {
            break;
        }
        if (n < 0 || !WriteAll(to, out, buffer.data(), static_cast<size_t>(n))) {
            ok = false;
            break;
        }
    }
    from.CloseFile(in);
    const bool closed = to.CloseFile(out) == 0;
    if (!ok || !closed) {
        outReason = "copying the data failed";
        return false;
    }
    return true;
}

// Removes what is at normalizedPath the way `rm -rf` does: a file, a symbolic
// link, or a directory after everything beneath it. A link is removed itself
// and never descended into. Stat and ReadDirectory both follow links, so a
// link to a directory looks like that directory -- and descending into it once
// deleted whatever it pointed at, wherever that was, before failing on the link.
bool RemoveTree(IFileSystem& fs, const std::string& normalizedPath, std::string& outReason) {
    FileStatus status;
    // What cannot be stat'ed at all -- a link that leads nowhere, say -- is
    // removed as the file it is.
    const bool isDirectory = fs.Stat(normalizedPath, status) == 0 &&
        status.type == DirectoryEntryType::Dir && !status.symbolicLink;
    if (!isDirectory) {
        if (fs.RemoveFile(normalizedPath) == 0) {
            return true;
        }
        // On Windows a link to a directory is removed with rmdir, which takes
        // the link away and leaves the directory it points at alone.
        if (status.symbolicLink && fs.RemoveDirectory(normalizedPath) == 0) {
            return true;
        }
        outReason = "cannot remove file " + normalizedPath;
        return false;
    }
    for (const auto& entry : fs.ReadDirectory(normalizedPath)) {
        if (entry.name == "." || entry.name == "..") {
            continue;
        }
        if (!RemoveTree(fs, normalizedPath + "/" + entry.name, outReason)) {
            return false;
        }
    }
    if (fs.RemoveDirectory(normalizedPath) != 0) {
        outReason = "cannot remove directory " + normalizedPath;
        return false;
    }
    return true;
}

// A host file, as the directory holding it (reached through a physical
// filesystem jailed there) and its name within it.
struct HostFile {
    std::filesystem::path absolute;
    std::string directory;
    std::string nameInDirectory;
};

bool ResolveHostFile(const std::filesystem::path& haisosFileDir, const std::string& hostPath, HostFile& out, std::string& outReason) {
    // operator/ keeps an absolute hostPath as it is.
    out.absolute = (haisosFileDir / hostPath).lexically_normal();
    if (!out.absolute.has_filename()) {
        outReason = "the host path '" + hostPath + "' does not name a file";
        return false;
    }
    out.directory = out.absolute.parent_path().string();
    out.nameInDirectory = "/" + out.absolute.filename().string();
    return true;
}

std::string JoinNames(const std::vector<std::string>& names) {
    std::string joined;
    for (const auto& name : names) {
        joined += (joined.empty() ? "" : ", ") + name;
    }
    return joined;
}

bool PlaceBuiltin(const HaisosFileOperation& operation, const HaisosFileBuiltinTargets& builtins, std::string& outReason) {
    if (!builtins.namedFileSystems || !builtins.builtinCommands || !builtins.configurator) {
        outReason = "builtin commands are not available here";
        return false;
    }
    auto fs = builtins.namedFileSystems->find(operation.fsName);
    if (fs == builtins.namedFileSystems->end()) {
        outReason = "unknown filesystem '" + operation.fsName + "' (BUILTIN names a filesystem declared with FS)";
        return false;
    }
    const auto commands = builtins.builtinCommands->GetCommands();
    if (std::find(commands.begin(), commands.end(), operation.builtinName) == commands.end()) {
        outReason = "unknown builtin '" + operation.builtinName + "' (the builtins are: " + JoinNames(commands) + ")";
        return false;
    }
    return builtins.configurator->AddBuiltinCommand(fs->second, operation.path, operation.builtinName, &outReason);
}

bool ApplyOne(
    IFactory& factory,
    IFileSystem& root,
    const HaisosFileOperation& operation,
    const std::filesystem::path& haisosFileDir,
    const HaisosFileBuiltinTargets& builtins,
    std::string& outReason)
{
    const std::string path = NormalizeVirtualPath(operation.path);
    switch (operation.type) {
        case HaisosFileOperationType::CreateDir:
            return EnsureDirectories(root, path, outReason);

        case HaisosFileOperationType::Builtin:
            return PlaceBuiltin(operation, builtins, outReason);

        case HaisosFileOperationType::Create:
        case HaisosFileOperationType::Append:
            if (path == "/") {
                outReason = "the root is a directory";
                return false;
            }
            return WriteContent(root, path, operation.content, operation.type == HaisosFileOperationType::Append, outReason);

        case HaisosFileOperationType::Delete: {
            if (path == "/") {
                outReason = "the root directory cannot be deleted";
                return false;
            }
            if (!EntryTypeOf(root, path)) {
                outReason = "it does not exist";
                return false;
            }
            return RemoveTree(root, path, outReason);
        }

        case HaisosFileOperationType::Copy: {
            HostFile host;
            if (!ResolveHostFile(haisosFileDir, operation.hostPath, host, outReason)) {
                return false;
            }
            auto hostFs = factory.CreatePhysicalFileSystem(host.directory);
            return CopyFileBetween(*hostFs, host.nameInDirectory, root, path, outReason);
        }

        case HaisosFileOperationType::OutCopy: {
            HostFile host;
            if (!ResolveHostFile(haisosFileDir, operation.hostPath, host, outReason)) {
                return false;
            }
            // The host directory is created before a filesystem is rooted in
            // it: a physical filesystem cannot reach above its own root to
            // create the directories leading down to it.
            std::error_code ec;
            std::filesystem::create_directories(host.directory, ec);
            if (ec) {
                outReason = "cannot create host directory " + host.directory + ": " + ec.message();
                return false;
            }
            auto hostFs = factory.CreatePhysicalFileSystem(host.directory);
            return CopyFileBetween(root, path, *hostFs, host.nameInDirectory, outReason);
        }
    }
    outReason = "unknown directive";
    return false;
}

} // namespace

bool ApplyHaisosFileOperations(
    IFactory& factory,
    IFileSystem& rootFileSystem,
    const std::vector<HaisosFileOperation>& operations,
    const std::filesystem::path& haisosFileDir,
    std::string& outError,
    const HaisosFileBuiltinTargets& builtins)
{
    for (const auto& operation : operations) {
        const char* name = DirectiveName(operation.type);
        LogDebug("HaisosFileOperations: line %d: %s %s %s", operation.lineNumber, name,
            operation.path.c_str(), operation.hostPath.c_str());
        std::string reason;
        if (!ApplyOne(factory, rootFileSystem, operation, haisosFileDir, builtins, reason)) {
            outError = "Error: line " + std::to_string(operation.lineNumber) + ": " + name + " " + operation.path +
                (operation.hostPath.empty() ? "" : " (host: " + operation.hostPath + ")") + " failed: " + reason + "\n";
            return false;
        }
    }
    return true;
}

} // namespace Haisos
