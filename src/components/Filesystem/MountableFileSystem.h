#pragma once
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include "MountPoints.h"
#include "interfaces/IFileSystemService.h"

namespace Haisos {

// Implements IFileSystem's mount routing once, so every filesystem supports
// Mount/Unmount identically instead of six classes each re-deriving it.
//
// A subclass implements the Local* operations, which see only the paths this
// filesystem itself owns: anything at or under a mount point has already been
// routed away by the time they are called.
class MountableFileSystem : public IFileSystem {
public:
    void Mount(const std::string& whereToMount, std::shared_ptr<IFileSystem> toBeMounted) final;
    void Unmount(const std::string& mountedPath) final;

    std::shared_ptr<IFileDescriptor> OpenFile(const std::string& pathname, int flags) final;
    std::shared_ptr<IFileDescriptor> OpenFile(const std::string& pathname, int flags, int mode) final;
    int CreateDirectory(const std::string& pathname, int mode) final;
    int RemoveDirectory(const std::string& pathname) final;
    int RemoveFile(const std::string& pathname) final;
    std::vector<DirectoryEntry> ReadDirectory(const std::string& path) final;
    int Stat(const std::string& path, FileStatus& out) final;

    int AddBuiltinCommand(const std::string& path, const std::string& builtinName) final;
    int RemoveBuiltinCommand(const std::string& path) final;
    std::optional<std::string> IsBuiltinCommand(const std::string& path) final;

protected:
    // How this filesystem addresses |path| absolutely (resolved against its own
    // root -- a filesystem has no current directory; see IFileSystem). Mount
    // points are matched against this, so a subclass and its mounts agree on
    // what a path means.
    virtual std::string AbsolutePathFor(const std::string& path) const = 0;

    virtual std::shared_ptr<IFileDescriptor> LocalOpenFile(const std::string& pathname, int flags) = 0;
    virtual std::shared_ptr<IFileDescriptor> LocalOpenFile(const std::string& pathname, int flags, int mode) = 0;
    virtual int LocalCreateDirectory(const std::string& pathname, int mode) = 0;
    virtual int LocalRemoveDirectory(const std::string& pathname) = 0;
    virtual int LocalRemoveFile(const std::string& pathname) = 0;
    virtual std::vector<DirectoryEntry> LocalReadDirectory(const std::string& path) = 0;
    virtual int LocalStat(const std::string& path, FileStatus& out) = 0;

    // Asked by IsBuiltinCommand for a path that is neither in this
    // filesystem's own builtin list nor under a mount point. A filesystem that
    // wraps another one (read-only, sub-path, composed) passes the
    // question on to it; one with nothing underneath has no builtins but its
    // own, which is the default.
    virtual std::optional<std::string> LocalIsBuiltinCommand(const std::string& /*path*/) { return std::nullopt; }

    // Whether AddBuiltinCommand may place a builtin here at all. A filesystem
    // holding nothing but what it was made with (a device filesystem) says no.
    virtual bool LocalCanHoldBuiltinCommands() const { return true; }

private:
    // If one of this filesystem's own builtins is at |absolute|, opens it --
    // read-only, so asking to write fails with a null outFile -- and returns
    // true; otherwise returns false and the path is someone else's to open.
    bool OpenOwnBuiltin(const std::string& absolute, int flags, std::shared_ptr<IFileDescriptor>& outFile);
    // The builtin this filesystem itself placed at |absolute|, if any.
    std::optional<std::string> OwnBuiltinAt(const std::string& absolute) const;
    // Whether any of this filesystem's own builtins is at or below |absolute|,
    // which is what pins a directory in place.
    bool HasOwnBuiltinAtOrUnder(const std::string& absolute) const;
    // Adds this filesystem's own builtins that live directly in |absolute| to
    // a listing of it.
    void AddOwnBuiltinEntries(const std::string& absolute, std::vector<DirectoryEntry>& entries) const;
    // Makes "." and ".." the first two entries of a listing of |path| -- once,
    // whether or not whatever served it put them in already -- if |path| is a
    // directory.
    void PutDotEntriesFirst(const std::string& path, std::vector<DirectoryEntry>& entries);

    MountPoints m_mounts;

    mutable std::mutex m_builtinsMutex;
    struct Builtin {
        std::string name;
        // When it was placed, which is what Stat reports as its times.
        FileDateTime placedTime;
    };
    // Absolute path -> builtin, for the builtins placed on this filesystem
    // itself (not those seen through a mount or an underlying one).
    std::map<std::string, Builtin> m_builtins;
};

}
