#pragma once
#include <string>
#include "interfaces/IFileSystemService.h"
#include "src/components/Filesystem/VirtualPath.h"

namespace Haisos {

// A process's working directory, shared by every IProcess implementation. A
// filesystem has none of its own (see IFileSystem), so this is the only thing a
// relative path is resolved against -- and it is per-process, so one process
// moving does not move any other.

// The directory a process starts in: an absolute path within the OS's root,
// defaulting to the root itself when nothing was asked for.
inline std::string NormalizeWorkingDirectory(const std::string& workingDirectory) {
    return workingDirectory.empty() ? std::string("/") : NormalizeVirtualPath(workingDirectory);
}

// Moves |current| to |path|, resolved against |current|. Returns 0 and updates
// |current| on success, or -1 and leaves it alone if the target is not a
// directory of |rootFileSystem| -- matching chdir(). "/" always succeeds: it is
// the root the process can never step outside of.
inline int ChangeWorkingDirectory(IFileSystem* rootFileSystem, std::string& current, const std::string& path) {
    std::string target = NormalizeVirtualPath(path, current);
    if (target != "/") {
        if (!rootFileSystem) {
            return -1;
        }
        // A directory listing is what tells the two apart: a file lists as
        // nothing, and so does a path that is not there at all.
        auto parent = target.substr(0, target.find_last_of('/'));
        auto entries = rootFileSystem->ReadDirectory(parent.empty() ? "/" : parent);
        auto name = target.substr(target.find_last_of('/') + 1);
        bool isDirectory = false;
        for (const auto& entry : entries) {
            if (entry.name == name && entry.type == DirectoryEntryType::Dir) {
                isDirectory = true;
                break;
            }
        }
        if (!isDirectory) {
            return -1;
        }
    }
    current = target;
    return 0;
}

}
