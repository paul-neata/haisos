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

// The same, reported under |reportedName|: "$VERSION_CONTROL" for the
// environment's word ("backup type" is ParseBackupControl's).
bool ParseBackupControlNamed(BuiltinContext& context, const std::string& reportedName,
                             const std::string& word, BackupMode& out);

// Parses a --update UPDATE word: all, none, older. On a bad word prints
// GNU's block and returns false (the exit status is then 1).
bool ParseUpdateWord(BuiltinContext& context, const std::string& word, UpdateMode& update);

// The backup options of cp and mv, resolved once, after the option parsing, as
// GNU does: --backup=WORD is checked where it is met (ParseBackupControl),
// while -b and -S only turn backups on -- a bad $VERSION_CONTROL is then
// reported under that name, -S included -- and the suffix comes from -S or
// $SIMPLE_BACKUP_SUFFIX, else "~".
struct BackupRequest {
    bool on = false;         // -b, --backup or -S given
    bool wordSeen = false;   // --backup=WORD given (its mode already in |mode|)
    BackupMode mode = BackupMode::Existing;
    std::string suffix;      // -S's value; the default is filled in below
};

// What $VERSION_CONTROL says into |mode| when --backup=WORD was not given,
// and |suffix| from $SIMPLE_BACKUP_SUFFIX (else "~"). Returns false when a
// $VERSION_CONTROL word is refused (the exit status is then 1).
bool FinishBackupRequest(BuiltinContext& context, BackupRequest& request);

// GNU's quoteaf: every name in a cp or mv message, quoted even when plain.
std::string CopyQuoted(const std::string& name);

// dir + "/" + name, with no doubled slash ("/" + "x" is "/x").
std::string CopyJoinPath(const std::string& dir, const std::string& name);

// Why Stat(path) found nothing: "Not a directory" when a segment on the way is
// a file, else "No such file or directory".
std::string CopyStatMissingReason(IFileIO& io, const std::string& path);

// Why creating |path| failed: "No such file or directory" when its directory is
// missing, "Not a directory" when that is a file, else -- a builtin's path, a
// read-only filesystem -- "Permission denied" (IFileIO gives no reason).
std::string CopyCreateFailedReason(IFileIO& io, const std::string& path);

} // namespace Haisos