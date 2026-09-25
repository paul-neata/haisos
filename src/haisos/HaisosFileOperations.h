#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "interfaces/IFactory.h"
#include "HaisosFileParser.h"

namespace Haisos {

// Applies CREATE/APPEND/COPY/DELETE/OUTCOPY directives to rootFileSystem, in
// order, stopping at the first that fails: returns false and fills outError
// (naming the directive's line) if one does.
//
//   CREATE/APPEND  write/append the content, creating missing parent directories
//   COPY           copy a host file into the root, creating missing parents
//   DELETE         remove a file, or a directory and everything beneath it;
//                  the path must exist, and may not be the root itself
//   OUTCOPY        copy a file out of the root onto the host, creating missing
//                  host directories
//
// A host path is resolved against haisosFileDir when relative, the way FS
// PHYSICAL paths are; host files are reached through physical filesystems from
// factory, never directly. COPY and OUTCOPY copy files, not directories.
bool ApplyHaisosFileOperations(
    IFactory& factory,
    IFileSystem& rootFileSystem,
    const std::vector<HaisosFileOperation>& operations,
    const std::filesystem::path& haisosFileDir,
    std::string& outError);

} // namespace Haisos
