#pragma once
#include <optional>
#include <string>
#include <vector>
#include "BuiltinCommand.h"

namespace Haisos {

class BuiltinPrompt;

enum class BackupMode { None, Simple, Numbered, Existing };
enum class UpdateMode { All, None, Older };        // --update=all|none|older
enum class CopyVerbose { None, Cp, Mv };           // see "Verbose lines" in BuiltinCopy.cpp

struct CopyOptions {
    bool recursive = false;          // -r/-R/-a: copy directories
    bool preserveTimes = false;      // -p, -a, --preserve=timestamps/all
    bool attributesOnly = false;     // --attributes-only: no data
    bool removeDestination = false;  // --remove-destination
    bool force = false;              // -f: unwritable destination removed and retried
    bool interactive = false;        // -i: ask before overwriting an existing file
    UpdateMode update = UpdateMode::All;  // -n is UpdateMode::None, -u UpdateMode::Older
    BackupMode backup = BackupMode::None;
    std::string backupSuffix = "~";
    CopyVerbose verbose = CopyVerbose::None;
};

// One source and the exact path its copy gets (never "the directory to copy
// into"), both as the user is shown them.
struct CopyTarget {
    std::string source;
    std::string dest;
};

// cp's and mv's operand rules (see BuiltinCopy.cpp). Reports GNU's messages with
// context.Name() and returns nullopt on an error, the exit status then 1.
std::optional<std::vector<CopyTarget>> ResolveCopyTargets(
    BuiltinContext& context, std::vector<std::string> operands,
    const std::optional<std::string>& targetDirectory, bool noTargetDirectory,
    bool parents, bool stripTrailingSlashes);

// Copies |source| to |dest| -- a file, or with options.recursive a directory
// and its whole tree -- applying options at every level as cp does. |prompt|
// may be null when options.interactive is false. Messages begin with
// context.Name(). Returns false if anything was not copied, a declined
// overwrite included (GNU 9.4 then exits 1); a file skipped by -n/-u is not a
// failure.
bool CopyPath(BuiltinContext& context, BuiltinPrompt* prompt,
              const std::string& source, const std::string& dest,
              const CopyOptions& options);

// The name a backup of |dest| gets (see BuiltinCopy.cpp); |dest| exists.
std::string BackupPathFor(BuiltinContext& context, const std::string& dest,
                          BackupMode mode, const std::string& suffix);

// Parses a --backup CONTROL word: none/off, simple/never, existing/nil,
// numbered/t. On a bad word prints GNU's block and returns false (the exit
// status is then 1).
bool ParseBackupControl(BuiltinContext& context, const std::string& word, BackupMode& out);

} // namespace Haisos