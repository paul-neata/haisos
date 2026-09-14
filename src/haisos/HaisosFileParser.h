#pragma once

#include <string>
#include <utility>
#include <vector>

namespace Haisos {

struct HaisosFileRunEntry {
    std::string programPath;
    std::vector<std::string> args;
};

// One "FS <name> <type> <args...>" directive. type is one of "PHYSICAL",
// "RO", "MEM", "SUB" (validated by the parser); args holds whatever follows,
// already substituted:
//   PHYSICAL <folder>            args = {folder}
//   RO <other_fs>                args = {other_fs}
//   MEM                          args = {}
//   SUB <other_fs> <folder>      args = {other_fs, folder}
// Building the actual IFileSystem is left to the caller (the parser has no
// access to IFactory/IFilesystemService).
struct HaisosFileFilesystemDecl {
    std::string name;
    std::string type;
    std::vector<std::string> args;
};

// One "MOUNT <main_fs> <path> <fs_to_mount>" directive.
struct HaisosFileMount {
    std::string mainFs;
    std::string path;
    std::string toBeMountedFs;
};

// A single ordered step, since FS and MOUNT directives interleave and their
// relative order matters when building the real filesystems (a MOUNT can
// retarget a name that a later SUB then builds on).
struct HaisosFileFsStep {
    bool isMount = false;
    HaisosFileFilesystemDecl declare; // valid when !isMount
    HaisosFileMount mount;            // valid when isMount
};

// One ENV directive. `ENV NAME=value` sets the variable outright;
// `ENV NAME` (no '=') imports NAME from the host OS's environment, which is
// the only way a host variable reaches the Haisos OS. Importing a name the
// host does not define leaves it unset rather than failing.
struct HaisosFileEnvEntry {
    std::string name;
    std::string value;
    bool importFromHost = false;
};

struct HaisosFileConfig {
    // The value given after ROOT, or empty if omitted. Resolved by the
    // caller: if it names a filesystem declared via FS, that filesystem is
    // the root; otherwise (no FS directives at all -- legacy shorthand) it is
    // a plain directory path, mounted via IFactory::CreatePhysicalFileSystem.
    // Empty with no FS directives means "use the haisosfile's own directory";
    // empty with FS directives means "use the last one declared".
    std::string rootPath;
    std::vector<HaisosFileFsStep> fsSteps;
    std::vector<HaisosFileRunEntry> runEntries;
    // ENV directives, in file order. Applied to the OS's initial environment,
    // which every process and sub-OS then inherits.
    std::vector<HaisosFileEnvEntry> envEntries;
};

struct HaisosFileParseResult {
    HaisosFileConfig config;
    std::string error;
};

// Parses a haisosfile's content. argOverrides are "name"->"value" pairs (from
// the CLI's `-- name=value ...`) that override an ARG directive's default.
HaisosFileParseResult ParseHaisosFile(
    const std::string& content,
    const std::vector<std::pair<std::string, std::string>>& argOverrides);

// A small, heavily-commented starter haisosfile (see `haisos --init`).
std::string GetHaisosFileTemplate();

} // namespace Haisos
