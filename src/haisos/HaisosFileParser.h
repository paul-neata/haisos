#pragma once

#include <string>
#include <utility>
#include <vector>

namespace Haisos {

// One "RUN [-i] <absolute_program_path> <args...>" directive. The program
// starts at "/", the OS's root. interactive is set by "-i": see
// StartProcessOptions::interactiveAgent.
struct HaisosFileRunEntry {
    std::string programPath;
    std::vector<std::string> args;
    bool interactive = false;
};

// A directive acting on the files of the OS's root filesystem.
enum class HaisosFileOperationType {
    Create,  // CREATE <path> <content>: write content to path, replacing it
    Append,  // APPEND <path> <content>: append content to path, creating it if missing
    Copy,    // COPY <host_path> <path>: copy a host file into the root
    Delete,  // DELETE <path>: remove a file, or a directory and all it holds
    OutCopy, // OUTCOPY <path> <host_path>: copy a file out of the root to the host
};

// One CREATE/APPEND/COPY/DELETE/OUTCOPY directive, already substituted.
// path is always the one in the Haisos OS, and absolute; hostPath is the one on
// the real disk, resolved by the caller against the haisosfile's directory
// when relative (as for FS PHYSICAL).
struct HaisosFileOperation {
    HaisosFileOperationType type = HaisosFileOperationType::Create;
    std::string path;
    std::string hostPath; // COPY's source, OUTCOPY's destination
    std::string content;  // CREATE/APPEND
    int lineNumber = 0;   // for error messages
};

// One "FS <name> <type> <args...>" directive. type is one of "PHYSICAL",
// "RO", "MEM", "SUB" (validated by the parser); args holds whatever follows,
// already substituted:
//   PHYSICAL <folder>            args = {folder}
//   RO <other_fs>                args = {other_fs}
//   MEM                          args = {}
//   SUB <other_fs> <folder>      args = {other_fs, folder}
// Building the actual IFileSystem is left to the caller (the parser has no
// access to IFactory/IFileSystemService).
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
    // CREATE/APPEND/COPY/DELETE directives, in file order: applied to the root
    // filesystem once it is built, before any RUN process starts.
    std::vector<HaisosFileOperation> setupOperations;
    // OUTCOPY directives, in file order: applied once every RUN process has
    // finished, to pull what they produced out of the OS.
    std::vector<HaisosFileOperation> outCopyOperations;
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
