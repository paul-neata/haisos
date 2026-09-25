#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "interfaces/IFactory.h"
#include "HaisosFileParser.h"

namespace Haisos {

// What BUILTIN directives act on: the filesystems they may name (see
// BuildRootFileSystem), the builtins that exist, and what places them.
// Default-constructed, there is none of it, and a BUILTIN fails.
struct HaisosFileBuiltinTargets {
    const std::unordered_map<std::string, std::shared_ptr<IFileSystem>>* namedFileSystems = nullptr;
    IBuiltinCommands* builtinCommands = nullptr;
    IBuiltinConfigurator* configurator = nullptr;
};

// Applies file directives to rootFileSystem, in order, stopping at the first
// that fails: returns false and fills outError (naming the directive's line)
// if one does.
//
//   CREATE/APPEND  write/append the content, creating missing parent directories
//   CREATE_DIR     create a directory and any missing parents; one already
//                  there is fine, a file in the way is not
//   BUILTIN        place a builtin, by name, on the declared FS named (not
//                  necessarily the root), through builtins.configurator
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
    std::string& outError,
    const HaisosFileBuiltinTargets& builtins = HaisosFileBuiltinTargets{});

} // namespace Haisos
