#include <sys/stat.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinCopy.h"
#include "BuiltinText.h"
#include "commands/patch/PatchApply.h"
#include "commands/patch/PatchParse.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {

namespace {

#ifdef _WIN32
constexpr int kPatchDirMode = _S_IREAD | _S_IWRITE;
#else
constexpr int kPatchDirMode = S_IRWXU | S_IRWXG | S_IRWXO;
#endif

enum PatchOptionId {
    kPatchDirectory = 1,        // -d
    kPatchRemoveEmpty,          // -E
    kPatchForce,                // -f
    kPatchForward,              // -N
    kPatchInput,                // -i
    kPatchOutput,               // -o
    kPatchReverse,              // -R
    kPatchStrip,                // -p
    kPatchSilent,               // -s / --quiet
    kPatchBatch,                // -t
    kPatchUnified,              // -u
    kPatchVersionOption,        // -v
    kPatchDryRun,               // --dry-run
    kPatchBinary,               // --binary
    kPatchFuzz,                 // -F
    kPatchLooseWhitespace,      // -l
    kPatchBackup,               // -b
    kPatchSuffix,               // -z
    kPatchVersionControl,       // -V
    kPatchBackupIfMismatch,     // --backup-if-mismatch
    kPatchNoBackupIfMismatch,   // --no-backup-if-mismatch
    kPatchRejectFile,           // -r
    kPatchRejectFormat,         // --reject-format
    kPatchContext,              // -c
    kPatchNormal,               // -n
};

// One run of the patch command: its options, and what the file patches so far
// have left behind.
struct PatchRun {
    BuiltinContext& context;
    bool reverse = false;    // -R
    bool forward = false;    // -N
    bool force = false;      // -f
    bool batch = false;      // -t
    bool silent = false;     // -s
    bool dryRun = false;     // --dry-run
    bool removeEmpty = false;  // -E
    bool binary = false;       // --binary
    bool stripGiven = false;   // -p was on the command line
    bool looseWhitespace = false;  // -l
    int64_t fuzz = 2;             // -F NUM (GNU's own default 2)
    bool backup = false;           // -b
    bool mismatchBackup = true;    // --backup-if-mismatch, on by default
    std::optional<std::string> outputFile;   // -o FILE
    std::optional<std::string> origOperand;  // the ORIGFILE operand
    std::optional<std::string> rejectFile;   // -r FILE ("-" discards them)
    std::optional<RejFormat> rejectFormat;   // --reject-format
    BackupMode backupMode = BackupMode::Existing;  // -V / the environment,
    // decided after the options are parsed
    std::string backupSuffix;  // -z / SIMPLE_BACKUP_SUFFIX, likewise
    int status = 0;  // 1 when a hunk failed or was ignored, a file patch was
    // skipped, or a deletion was refused
    std::set<std::string> backupMade;   // targets whose backup was written
    std::set<std::string> rejWritten;   // reject files written this run
    std::shared_ptr<IFileDescriptor> outFile;  // -o FILE, opened once

    explicit PatchRun(BuiltinContext& context) : context(context) {}

    void Out(const std::string& text) { context.Out(text); }
    void Fatal(const std::string& text) { context.Error("**** " + text); }

    // What is at |path|: false when nothing is. |size| is filled only then it
    // is there.
    bool Stat(const std::string& path, uint64_t& size) {
        FileStatus status;
        if (context.IO().Stat(path, status) != 0) {
            return false;
        }
        size = status.size;
        return true;
    }
    bool Exists(const std::string& path) {
        uint64_t size = 0;
        return Stat(path, size);
    }
};

// Whether |path| names a directory.
bool IsDirectory(PatchRun& run, const std::string& path) {
    FileStatus status;
    return run.context.IO().Stat(path, status) == 0
        && status.type == DirectoryEntryType::Dir;
}

// Why a file patch could not create |path|: it is a directory, its parent is
// missing, or nothing else is known (Haisos has no permissions to check).
std::string CreateFailedReason(PatchRun& run, const std::string& path) {
    if (IsDirectory(run, path)) {
        return "Is a directory";
    }
    const size_t slash = path.rfind('/');
    if (slash != std::string::npos && !run.Exists(path.substr(0, slash))) {
        return "No such file or directory";
    }
    return "Permission denied";
}

// Why the original could not be renamed onto |path| for a backup.
std::string BackupRenameFailedReason(PatchRun& run, const std::string& path) {
    return IsDirectory(run, path) ? "Is a directory" : "Permission denied";
}

// The lines of a whole file: each with its '\n', the last possibly without.
std::vector<std::string> SplitFileLines(const std::string& bytes) {
    std::vector<std::string> lines;
    std::string line;
    for (char c : bytes) {
        line += c;
        if (c == '\n') {
            lines.push_back(line);
            line.clear();
        }
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

std::string JoinFileLines(const std::vector<std::string>& lines) {
    std::string bytes;
    for (const std::string& line : lines) {
        bytes += line;
    }
    return bytes;
}

bool EndsCrLf(const std::string& line) {
    return line.size() >= 2 && line[line.size() - 2] == '\r' && line[line.size() - 1] == '\n';
}

// -p NUM and -F NUM: an optional '-', then digits; a value an int64 cannot
// hold saturates at its maximum (GNU's strtol does the same). |what| names
// the option in the message. Returns false after reporting.
bool ParseCountOption(BuiltinContext& context, const std::string& what,
                      const std::string& argument, int64_t& out) {
    bool ok = !argument.empty();
    bool negative = false;
    size_t i = 0;
    if (!argument.empty() && argument[0] == '-') {
        negative = true;
        i = 1;
    }
    int64_t value = 0;
    for (; ok && i < argument.size(); ++i) {
        const char c = argument[i];
        if (c < '0' || c > '9') {
            ok = false;
            break;
        }
        if (value > (INT64_MAX - (c - '0')) / 10) {
            value = INT64_MAX;
            continue;
        }
        value = value * 10 + (c - '0');
    }
    if (!ok) {
        context.Error("**** " + what + " " + ShellEscapeQuoted(argument) + " is not a number");
        return false;
    }
    if (negative) {
        context.Error("**** " + what + " " + ShellEscapeQuoted(argument) + " is negative");
        return false;
    }
    out = value;
    return true;
}

// The '/'-separated components of a file name, empty ones left out.
std::vector<std::string> NameComponents(const std::string& name) {
    std::vector<std::string> components;
    size_t start = 0;
    for (size_t i = 0; i <= name.size(); ++i) {
        if (i == name.size() || name[i] == '/') {
            if (i > start) {
                components.push_back(name.substr(start, i - start));
            }
            start = i + 1;
        }
    }
    return components;
}

// Whether |a| is a better file to patch than |b|: fewer path components, then
// a shorter last component, then a shorter name. A tie keeps |b| (the earlier
// candidate, as the order they were given in).
bool CandidateBetter(const std::string& a, const std::string& b) {
    const std::vector<std::string> aComponents = NameComponents(a);
    const std::vector<std::string> bComponents = NameComponents(b);
    if (aComponents.size() != bComponents.size()) {
        return aComponents.size() < bComponents.size();
    }
    const size_t aLast = aComponents.empty() ? 0 : aComponents.back().size();
    const size_t bLast = bComponents.empty() ? 0 : bComponents.back().size();
    if (aLast != bLast) {
        return aLast < bLast;
    }
    if (a.size() != b.size()) {
        return a.size() < b.size();
    }
    return false;
}

// How many of |name|'s directories do not exist yet.
size_t MissingDirectories(PatchRun& run, const std::string& name) {
    size_t missing = 0;
    size_t pos = 0;
    while ((pos = name.find('/', pos + 1)) != std::string::npos) {
        if (!run.Exists(name.substr(0, pos))) {
            ++missing;
        }
    }
    return missing;
}

// Creates |name|'s missing directories (a created file's parents).
void CreateMissingDirectories(PatchRun& run, const std::string& name) {
    size_t pos = 0;
    while ((pos = name.find('/', pos + 1)) != std::string::npos) {
        const std::string parent = name.substr(0, pos);
        if (!run.Exists(parent)) {
            run.context.IO().CreateDirectory(parent, kPatchDirMode);
        }
    }
}

// The file a patch names: the chosen name, whether it is there, and its size.
struct PatchFileChoice {
    bool any = false;  // a file name was chosen
    std::string name;
    bool exists = false;
    uint64_t size = 0;  // meaningful when |exists|
};

// The patch's candidate names, old and new (the Index name only when both
// header names are absent).
std::vector<std::string> CandidateNames(const FilePatch& patch) {
    std::vector<std::string> candidates;
    if (patch.oldName) {
        candidates.push_back(*patch.oldName);
    }
    if (patch.newName) {
        candidates.push_back(*patch.newName);
    }
    if (candidates.empty() && patch.indexName) {
        candidates.push_back(*patch.indexName);
    }
    return candidates;
}

// Which file |patch| is applied to. An ORIGFILE operand is the file for every
// patch. Otherwise, among the candidates that exist the best one; when none
// exists and the old side is absent (the patch creates its file), the
// candidate needing the fewest new directories.
PatchFileChoice ChooseFile(PatchRun& run, const FilePatch& patch) {
    PatchFileChoice choice;
    if (run.origOperand) {
        choice.any = true;
        choice.name = *run.origOperand;
        choice.exists = run.Stat(choice.name, choice.size);
        return choice;
    }
    const std::vector<std::string> candidates = CandidateNames(patch);
    const std::string* best = nullptr;
    for (const std::string& candidate : candidates) {
        if (run.Exists(candidate) && (!best || CandidateBetter(candidate, *best))) {
            best = &candidate;
        }
    }
    if (best) {
        choice.any = true;
        choice.name = *best;
        choice.exists = run.Stat(choice.name, choice.size);
        return choice;
    }
    if (!candidates.empty() && patch.oldAbsence != SideAbsence::Present) {
        const std::string* bestCreate = nullptr;
        size_t bestMissing = 0;
        for (const std::string& candidate : candidates) {
            const size_t missing = MissingDirectories(run, candidate);
            if (!bestCreate || missing < bestMissing) {
                bestCreate = &candidate;
                bestMissing = missing;
            }
        }
        choice.any = true;
        choice.name = *bestCreate;
    }
    return choice;
}

// The three ways a "perhaps reversed?" question can end: the patch skipped,
// applied after another swap of its sides, or applied as it stands.
enum class PatchAnswer { Skip, SwapAndApply, ApplyAsIs };

// One of the questions patch asks when it is not sure, printed on one line
// with the answer. |leadIn| is what came before on the same line (the
// reversal detection's message, or a conflict's two-line text). |underR|
// names the question's -R form. Nothing is read: the no-terminal answers are
// taken, whatever the options say.
PatchAnswer AskReversal(PatchRun& run, const std::string& leadIn, bool underR) {
    if (run.force) {
        run.Out(leadIn + "  Applying it anyway.\n");
        return PatchAnswer::ApplyAsIs;
    }
    if (run.batch) {
        if (underR) {
            run.Out(leadIn + "  Ignoring -R.\n");
        } else {
            run.Out(leadIn + "  Assuming -R.\n");
        }
        return PatchAnswer::SwapAndApply;
    }
    if (run.forward) {
        run.Out(leadIn + "  Skipping patch.\n");
        return PatchAnswer::Skip;
    }
    const std::string question = underR ? "  Ignore -R? [n] " : "  Assume -R? [n] ";
    run.Out(leadIn + question + "\n");
    run.Out("Apply anyway? [n] \n");
    if (!run.silent) {
        run.Out("Skipping patch.\n");
    }
    return PatchAnswer::Skip;
}

// The file a skipped or forced patch still names: the chosen name, or the
// first candidate when none was chosen.
std::string ConflictName(PatchRun& run, const FilePatch& patch, const PatchFileChoice& choice) {
    if (choice.any) {
        return choice.name;
    }
    const std::vector<std::string> candidates = CandidateNames(patch);
    return candidates.empty() ? std::string() : candidates.front();
}

// What step 2 asks about: a creation whose file is there and not empty, a
// deletion of a missing file, or a deletion of (or git absence against) an
// empty file -- the two-line text without "The next patch would " and its
// answers, or nullopt when the patch goes ahead.
std::optional<std::string> ConflictText(const FilePatch& patch, const PatchFileChoice& choice) {
    const bool oldSurely = patch.oldAbsence == SideAbsence::Surely;
    const bool newSurely = patch.newAbsence == SideAbsence::Surely;
    const bool gitNoHunks = patch.gitDiff && patch.hunks.empty();
    const std::string name = ShellEscapeQuoted(choice.name);
    if (choice.exists) {
        if (choice.size > 0 && oldSurely && !newSurely) {
            // A creation (old absent, new present) against a file with
            // content. Both sides absent is neither creation nor deletion.
            return "create the file " + name + ",\nwhich already exists!";
        }
        if (choice.size == 0 && newSurely && patch.oldAbsence == SideAbsence::Present
            && !gitNoHunks) {
            return "empty out the file " + name + ",\nwhich is already empty!";
        }
        if (choice.size == 0 && oldSurely && patch.hunks.empty()) {
            return "empty out the file " + name + ",\nwhich is already empty!";
        }
    } else if (newSurely && !choice.name.empty()
        && patch.oldAbsence == SideAbsence::Present) {
        // A patch absent on both sides is a creation, not a deletion.
        return "delete the file " + name + ",\nwhich does not exist!";
    }
    return std::nullopt;
}

// The N out of M line, with the .rej file named when the rejects were saved.
void PrintSummary(PatchRun& run, size_t count, size_t total, bool ignored,
                  const std::string& rejName) {
    std::string text = std::to_string(count) + " out of " + std::to_string(total)
        + (total == 1 ? " hunk " : " hunks ") + (ignored ? "ignored" : "FAILED");
    if (!run.dryRun && !rejName.empty()) {
        text += " -- saving rejects to file " + ShellEscapeQuoted(rejName) + "\n";
    } else {
        text += "\n";
    }
    run.Out(text);
}

// Writes the reject text: replaced by the first rejects of a run, appended to
// by later ones. Returns false with |failure| filled ("open" plus the reason,
// or "write") when the file could not be written.
bool WriteRejFile(PatchRun& run, const std::string& rejName, const std::string& rejText,
                  std::string& failure) {
    if (rejText.empty()) {
        return true;
    }
    const int flags = run.rejWritten.count(rejName) ? kFileOpenWriteCreateAppend
                                                    : kFileOpenWriteCreateTruncate;
    run.rejWritten.insert(rejName);
    auto file = run.context.IO().OpenFile(rejName, flags, kFileCreateMode);
    if (!file) {
        failure = CreateFailedReason(run, rejName);
        return false;
    }
    if (WriteFully(*file, rejText) < 0) {
        failure = "write";
        return false;
    }
    return true;
}

// The whole block of a patch that names no file to patch: the text leading up
// to it, and (with -f or -t) the notice, else the questions nobody reads.
void NoFileBlock(PatchRun& run, const FilePatch& patch) {
    if (!run.silent) {
        run.Out("can't find file to patch at input line "
            + std::to_string(patch.reportLine) + "\n");
        // A normal diff names no file at all, so no -p could have helped it;
        // a diff whose headers had names keeps the line even when -p ate
        // every component of them.
        if (patch.format != PatchFormat::Normal) {
            run.Out(run.stripGiven ? "Perhaps you used the wrong -p or --strip option?\n"
                                   : "Perhaps you should have used the -p or --strip option?\n");
        }
    }
    if (!patch.leadingText.empty()) {
        run.Out("The text leading up to this was:\n--------------------------\n");
        std::string line;
        for (char c : patch.leadingText) {
            line += c;
            if (c == '\n') {
                run.Out("|" + line);
                line.clear();
            }
        }
        if (!line.empty()) {
            run.Out("|" + line + "\n");
        }
        run.Out("--------------------------\n");
    }
    if (run.force || run.batch) {
        run.Out("No file to patch.  Skipping patch.\n");
    } else {
        run.Out("File to patch: \n");
        run.Out("Skip this patch? [y] \n");
        if (!run.silent) {
            run.Out("Skipping patch.\n");
        }
    }
}

// The hunk's line to compare the file's line with, for the "different line
// endings" note of a failure: its first old line.
const std::string* FirstOldLine(const PatchHunk& hunk) {
    for (const PatchLine& line : hunk.lines) {
        if (line.kind != PatchLineKind::Insert) {
            return &line.text;
        }
    }
    return nullptr;
}

// Whether the patch, applied to |inputBytes|, leaves an empty result and the
// patch says the file should then be gone.
bool ResultRemoved(const FilePatch& patch, const std::string& content, const PatchRun& run) {
    return content.empty()
        && (patch.newAbsence == SideAbsence::Surely || run.removeEmpty);
}

// One file patch, from its choice of file to its written output, rejects and
// summary. Returns 2 when the patch is malformed (the whole run stops);
// otherwise 0, the run's status carrying any failure or skip.
int ApplyFilePatch(PatchRun& run, FilePatch patch) {
    if (run.reverse) {
        SwapFilePatch(patch);
    }
    const size_t total = patch.hunks.size();

    // A context diff whose first hunk is malformed is refused before
    // anything else of its file patch is said.
    if (!patch.malformedBeforeFile.empty()) {
        run.context.Error("**** " + patch.malformedBeforeFile);
        return 2;
    }

    // A git rename or copy reads one file and writes another. Its OLD is
    // oldName (an ORIGFILE operand in its place), its NEW newName, whichever
    // way round -R left them.
    bool isRenameOrCopy = patch.gitRename || patch.gitCopy;
    bool removeOldAfter = false;
    std::string input;   // the file read
    std::string output;  // the file written
    std::string patchingSuffix;
    if (isRenameOrCopy && patch.newName) {
        output = *patch.newName;
        if (run.origOperand) {
            // The operand replaces the OLD side, there or not.
            input = *run.origOperand;
            patchingSuffix = patch.gitRename ? " (renamed from " : " (copied from ";
            patchingSuffix += ShellEscapeQuoted(input) + ")";
            if (patch.gitRename || run.Exists(input)) {
                removeOldAfter = patch.gitRename;
            } else {
                // A copy needs both files: with the source gone there is
                // nothing to read and nothing to write.
                run.Out("Cannot copy file without two valid file names\n");
                run.status = 1;
                if (total > 0) {
                    PrintSummary(run, total, total, true, "");
                }
                return 0;
            }
        } else if (patch.oldName) {
            input = *patch.oldName;
            if (run.Exists(input)) {
                patchingSuffix = patch.gitRename ? " (renamed from " : " (copied from ";
                patchingSuffix += ShellEscapeQuoted(input) + ")";
                removeOldAfter = patch.gitRename;
            } else if (patch.gitCopy) {
                // A copy needs both files: with the source gone there is
                // nothing to read and nothing to write.
                run.Out("Cannot copy file without two valid file names\n");
                run.status = 1;
                if (total > 0) {
                    PrintSummary(run, total, total, true, "");
                }
                return 0;
            } else if (run.Exists(output)) {
                patchingSuffix = " (already renamed from " + ShellEscapeQuoted(input) + ")";
                input = output;
            } else {
                isRenameOrCopy = false;  // neither side is there: the usual choice
            }
        } else {
            isRenameOrCopy = false;
        }
    } else {
        isRenameOrCopy = false;
    }

    // Which file, and whether it is one the patch may not touch as it says.
    PatchFileChoice choice;
    bool decided = false;  // step 2 already had its question answered
    if (!isRenameOrCopy) {
        choice = ChooseFile(run, patch);
        // A file with no chosen name can still be named by the patch's first
        // candidate, which is what a conflict is about.
        if (!choice.any) {
            choice.name = ConflictName(run, patch, choice);
        }
        const std::optional<std::string> conflict = ConflictText(patch, choice);
        if (conflict) {
            decided = true;
            const std::string leadIn = (run.reverse ? "The next patch, when reversed, would "
                                                    : "The next patch would ") + *conflict;
            const PatchAnswer answer = AskReversal(run, leadIn, run.reverse);
            if (answer == PatchAnswer::Skip) {
                run.status = 1;
                if (total > 0) {
                    PrintSummary(run, total, total, true, "");
                }
                return 0;
            }
            if (answer == PatchAnswer::SwapAndApply) {
                SwapFilePatch(patch);
                choice = ChooseFile(run, patch);
            }
        }
        if (!choice.any) {
            if (run.force && !choice.name.empty()) {
                // -f: the patch has its way even with no file there
            } else {
                NoFileBlock(run, patch);
                run.status = 1;
                if (total > 0) {
                    PrintSummary(run, total, total, true, "");
                }
                return 0;
            }
        }
        input = choice.name;
        output = choice.name;
    }

    // What is said before the hunks: the CR stripping of the patch, and the
    // patching line.
    if (!run.silent) {
        if (patch.stripTrailingCr) {
            run.Out("(Stripping trailing CRs from patch; use --binary to disable.)\n");
        }
        std::string line = run.dryRun ? "checking file " : "patching file ";
        if (run.outputFile) {
            // -o names its own file; a git rename or copy still says so.
            line += ShellEscapeQuoted(*run.outputFile) + (patchingSuffix.empty()
                ? " (read from " + ShellEscapeQuoted(input) + ")" : patchingSuffix);
        } else if (!patchingSuffix.empty()) {
            line += ShellEscapeQuoted(output) + patchingSuffix;
        } else {
            line += ShellEscapeQuoted(input);
        }
        run.Out(line + "\n");
    }

    // The file as it stands.
    std::string inputBytes;
    if (run.Exists(input)) {
        auto file = run.context.IO().OpenFile(input, kFileOpenReadOnly);
        if (!file) {
            run.Fatal("Can't open file " + ShellEscapeQuoted(input) + " : No such file or directory");
            return 2;
        }
        const WholeReadOutcome outcome = ReadWholeInput(run.context, *file, inputBytes);
        if (outcome == WholeReadOutcome::Stopped) {
            return 2;  // a quiet stop
        }
        if (outcome == WholeReadOutcome::Error) {
            run.Fatal("Can't open file " + ShellEscapeQuoted(input) + " : Input/output error");
            return 2;
        }
    } else if (patch.oldAbsence == SideAbsence::Present && !run.force
               && !isRenameOrCopy) {
        // No creation, and nothing forced: the file a normal patch needs. A
        // rename reads its source wherever it went: missing, it reads empty
        // and the hunks fail against nothing.
        run.Fatal("Can't open file " + ShellEscapeQuoted(input) + " : No such file or directory");
        return 2;
    }

    // The hunks, in order. A patch whose old side is surely absent is a
    // creation: on a file that is not empty its hunks cannot apply.
    const PatchTarget target{SplitFileLines(inputBytes)};
    const bool creationRefused = patch.oldAbsence == SideAbsence::Surely
        && patch.newAbsence != SideAbsence::Surely && !target.lines.empty();
    int64_t runningOffset = 0;
    int64_t consumedLines = 0;
    int64_t lineShift = 0;  // what the applied hunks before this one changed
    // the file's length by (the line numbers said and saved are the output's)
    size_t written = 0;  // input lines already copied into the result
    std::vector<std::string> result;
    std::vector<RejectedHunk> rejHunks;  // the failed/ignored hunks
    bool skipped = false;          // the whole file patch was skipped
    bool anyOffset = false;         // a hunk applied away from its stated place
    bool anyFuzz = false;           // a hunk applied with context dropped
    for (size_t h = 0; h < total; ++h) {
        if (run.context.StopRequested()) {
            return 2;  // a quiet stop
        }
        PatchHunk& hunk = patch.hunks[h];
        if (skipped) {
            rejHunks.push_back({h + 1, 0});  // a skipped patch's shifts are 0
            continue;
        }
        // The hunk is tried at every fuzz level from none up to the option's
        // (never more than its context can lose): a match at a lower level
        // wins, and a forward match beats a reversed one at the same level.
        const int64_t maxFuzz = std::min(run.fuzz,
            std::max(hunk.leadingContext, hunk.trailingContext));
        int64_t at = 0;
        int64_t fuzz = 0;
        for (int64_t f = 0; f <= maxFuzz && at == 0; ++f) {
            at = creationRefused ? 0
                : LocateHunk(target, hunk, {f, run.looseWhitespace}, consumedLines,
                             runningOffset);
            if (at != 0) {
                fuzz = f;
            }
            // Hunk #1 alone, when it fits nowhere at any level: perhaps the
            // patch was already applied and its sides want swapping.
            if (h == 0 && at == 0 && !run.force && !decided && !creationRefused) {
                SwapFilePatch(patch);
                int64_t probeOffset = 0;
                const int64_t probe = LocateHunk(target, patch.hunks[0],
                    {f, run.looseWhitespace}, 0, probeOffset);
                if (probe != 0) {
                    const char* detected = run.reverse
                        ? "Unreversed patch detected!"
                        : "Reversed (or previously applied) patch detected!";
                    const PatchAnswer answer = AskReversal(run, detected, run.reverse);
                    if (answer == PatchAnswer::Skip) {
                        SwapFilePatch(patch);  // back as given, for the .rej
                        skipped = true;
                        rejHunks.push_back({1, 0});
                        break;
                    }
                    decided = true;  // kept swapped, as the answer asked
                    at = probe;
                    fuzz = f;
                    runningOffset = probeOffset;
                } else {
                    SwapFilePatch(patch);
                }
            }
        }
        if (skipped) {
            continue;
        }
        if (at == 0) {
            rejHunks.push_back({h + 1, lineShift});
            if (!run.silent) {
                const int64_t expected = hunk.oldStart + lineShift;
                std::string text = "Hunk #" + std::to_string(h + 1) + " FAILED at "
                    + std::to_string(expected);
                const std::string* oldFirst = FirstOldLine(hunk);
                bool hunkCr = false;
                if (oldFirst) {
                    hunkCr = EndsCrLf(*oldFirst);
                }
                bool fileCr = false;
                if (expected >= 1 && expected <= static_cast<int64_t>(target.lines.size())) {
                    fileCr = EndsCrLf(target.lines[static_cast<size_t>(expected - 1)]);
                }
                if (oldFirst && hunkCr != fileCr) {
                    text += " (different line endings)";
                }
                run.Out(text + ".\n");
            }
            continue;
        }
        // An insertion with no old lines is placed, not searched for, so it
        // is never found "at an offset", however far its place was moved.
        const bool hasOldLines = FirstOldLine(hunk) != nullptr;
        const int64_t offset = at - hunk.oldStart;
        if (offset != 0 && hasOldLines) {
            anyOffset = true;
        }
        if (fuzz > 0) {
            anyFuzz = true;
        }
        if (!run.silent && ((offset != 0 && hasOldLines) || fuzz > 0)) {
            std::string text = "Hunk #" + std::to_string(h + 1) + " succeeded at "
                + std::to_string(at + lineShift);
            if (fuzz > 0) {
                text += " with fuzz " + std::to_string(fuzz);
            }
            if (offset != 0 && hasOldLines) {
                text += " (offset " + std::to_string(offset)
                    + (offset == 1 ? " line" : " lines") + ")";
            }
            run.Out(text + ".\n");
        }
        // The input lines before the hunk, then the hunk up to its last
        // change; its trailing context waits for what copies the rest. A
        // line an earlier hunk already wrote is left alone, a Delete on one
        // a no-op: the two hunks share their ground.
        while (written < static_cast<size_t>(at - 1)) {
            result.push_back(target.lines[written]);
            ++written;
        }
        size_t lastChange = 0;
        for (size_t i = 0; i < hunk.lines.size(); ++i) {
            if (hunk.lines[i].kind != PatchLineKind::Context) {
                lastChange = i;
            }
        }
        size_t index = static_cast<size_t>(at - 1);
        for (size_t i = 0; i < hunk.lines.size(); ++i) {
            if (i > lastChange) {
                break;
            }
            switch (hunk.lines[i].kind) {
                case PatchLineKind::Context:
                    if (index >= written) {
                        result.push_back(target.lines[index]);
                        written = index + 1;
                    }
                    ++index;
                    break;
                case PatchLineKind::Delete:
                    if (index >= written) {
                        written = index + 1;  // consumed, not copied
                    }
                    ++index;
                    break;
                case PatchLineKind::Insert:
                    result.push_back(hunk.lines[i].text);
                    break;
            }
        }
        consumedLines = at + hunk.oldCount - hunk.trailingContext - 1;
        lineShift += hunk.newCount - hunk.oldCount;
    }

    // The rest of the input, after the last hunk.
    while (written < target.lines.size()) {
        result.push_back(target.lines[written]);
        ++written;
    }

    // A malformed patch is fatal, after the messages of the hunks before it.
    if (!patch.malformed.empty()) {
        run.context.Error("**** " + patch.malformed);
        return 2;
    }

    const std::string content = JoinFileLines(result);
    if (!rejHunks.empty()) {
        run.status = 1;
    }

    // A patch whose new side is surely absent says the file should be gone;
    // with content left it is not, said before the summary (a patch skipped
    // at its question says nothing of it).
    const bool removeResult = ResultRemoved(patch, content, run);
    if (!skipped && !removeResult && patch.newAbsence == SideAbsence::Surely
        && !content.empty() && !run.outputFile && !run.dryRun) {
        if (!run.silent) {
            run.Out("Not deleting file " + ShellEscapeQuoted(output)
                + " as content differs from patch\n");
        }
        run.status = 1;
    }

    // The rejects, and the line that says where they went: -r's own file
    // (replaced by this run's first rejects, appended to after), the usual
    // NAME.rej, or nowhere when -r was "-".
    if (!rejHunks.empty()) {
        std::string rejName;
        if (run.rejectFile) {
            if (*run.rejectFile != "-") {
                rejName = *run.rejectFile;
            }
        } else {
            rejName = run.outputFile ? *run.outputFile + ".rej" : output + ".rej";
        }
        PrintSummary(run, rejHunks.size(), total, skipped, rejName);
        if (!rejName.empty() && !run.dryRun) {
            const RejFormat format = run.rejectFormat
                ? *run.rejectFormat
                : (patch.format == PatchFormat::Unified ? RejFormat::Unified
                                                        : RejFormat::Context);
            std::string failure;
            if (!WriteRejFile(run, rejName, RejText(patch, rejHunks, format), failure)) {
                if (failure == "write") {
                    run.Fatal("write error : Input/output error");
                } else {
                    run.Fatal("Can't create file " + ShellEscapeQuoted(rejName) + " : "
                        + failure);
                }
                return 2;
            }
        }
    }

    // A patch skipped at its question writes nothing: the file stands as it
    // was, and no backup of it is kept.
    if (skipped) {
        return 0;
    }

    // The output. Nothing is written under --dry-run; -o collects every
    // result in one file; otherwise a temporary next to the target is renamed
    // into place, the original moved to its backup just before.
    if (!run.dryRun) {
        if (run.outputFile) {
            if (run.outFile && WriteFully(*run.outFile, content) < 0) {
                run.Fatal("write error : Input/output error");
                return 2;
            }
        } else {
            CreateMissingDirectories(run, output);
            const size_t slash = output.rfind('/');
            const std::string tmp = (slash == std::string::npos
                    ? "." + output
                    : output.substr(0, slash + 1) + "." + output.substr(slash + 1))
                + ".patchtmp";
            auto file = run.context.IO().OpenFile(tmp, kFileOpenWriteCreateTruncate, kFileCreateMode);
            if (!file) {
                run.Fatal("Can't create file " + ShellEscapeQuoted(tmp)
                    + " : No such file or directory");
                return 2;
            }
            const ssize_t wrote = WriteFully(*file, content);
            file.reset();
            if (wrote < 0) {
                // A partial result never replaces the file.
                run.context.IO().RemoveFile(tmp);
                run.Fatal("Can't write file " + ShellEscapeQuoted(tmp) + " : Input/output error");
                return 2;
            }
            // The backups, once per file per run: -b asks for them always, a
            // patch that did not apply cleanly otherwise (unless told not
            // to). What the new content replaces is moved to its backup just
            // before it takes its place -- the file written, an empty backup
            // when it was not there -- and a git rename's source, which the
            // rename takes away, under its own backup name first; a copy's
            // source is left alone. The key is the file, not the backup's
            // name: a numbered backup of a file patched twice in one run
            // still counts once.
            const bool mismatch = anyOffset || anyFuzz || !rejHunks.empty();
            if (run.backup || (run.mismatchBackup && mismatch)) {
                std::vector<std::string> originals;
                if (removeOldAfter && input != output && run.Exists(input)) {
                    originals.push_back(input);
                }
                originals.push_back(output);
                for (const std::string& original : originals) {
                    if (!run.backupMade.insert(run.context.IO().ResolvePath(original)).second) {
                        continue;
                    }
                    const std::string backupPath = BackupPathFor(run.context, original,
                        run.backupMode, run.backupSuffix);
                    const bool exists = run.Exists(original);
                    std::string backupFailure;
                    if (exists) {
                        if (run.context.IO().Rename(original, backupPath) != 0) {
                            backupFailure = BackupRenameFailedReason(run, backupPath);
                        }
                    } else if (!run.context.IO().OpenFile(backupPath,
                                   kFileOpenWriteCreateTruncate, kFileCreateMode)) {
                        backupFailure = CreateFailedReason(run, backupPath);
                    }
                    if (!backupFailure.empty()) {
                        // The file is left exactly as it was.
                        run.context.IO().RemoveFile(tmp);
                        if (exists) {
                            run.Fatal("Can't rename file " + ShellEscapeQuoted(original) + " to "
                                + ShellEscapeQuoted(backupPath) + " : " + backupFailure);
                        } else {
                            run.Fatal("Can't create file " + ShellEscapeQuoted(backupPath)
                                + " : " + backupFailure);
                        }
                        return 2;
                    }
                }
            }
            if (run.context.IO().Rename(tmp, output) != 0) {
                run.context.IO().RemoveFile(tmp);
                run.Fatal("Can't create file " + ShellEscapeQuoted(output)
                    + " : No such file or directory");
                return 2;
            }
        }
        // With -o the result went elsewhere: the renamed file stays.
        if (removeOldAfter && !run.outputFile) {
            run.context.IO().RemoveFile(input);
        }
        if (!run.outputFile && removeResult) {
            run.context.IO().RemoveFile(output);
        }
    }
    return 0;
}

} // namespace

class PatchCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "patch"; }
    std::string Version() const override { return "1.1.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'b', "backup", kPatchBackup, BuiltinArgument::None, "",
                "back the original up, however cleanly the patch applies"},
            {'B', "prefix", kBuiltinNotTreated, BuiltinArgument::Required, "PREFIX", ""},
            {'c', "context", kPatchContext, BuiltinArgument::None, "",
                "take the patch as a context diff (both of its styles)"},
            {'d', "directory", kPatchDirectory, BuiltinArgument::Required, "DIR",
                "read and write files in directory DIR"},
            {'D', "ifdef", kBuiltinNotTreated, BuiltinArgument::Required, "NAME", ""},
            {'e', "ed", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {'E', "remove-empty-files", kPatchRemoveEmpty, BuiltinArgument::None, "",
                "remove files left empty by patching"},
            {'f', "force", kPatchForce, BuiltinArgument::None, "",
                "do not ask questions; take every patch as it comes"},
            {'F', "fuzz", kPatchFuzz, BuiltinArgument::Required, "NUM",
                "let hunks match with up to NUM context lines dropped (default 2)"},
            {'g', "get", kBuiltinNotTreated, BuiltinArgument::Required, "NUM", ""},
            {'i', "input", kPatchInput, BuiltinArgument::Required, "FILE",
                "read the patch from FILE ('-' the standard input)"},
            {'l', "ignore-whitespace", kPatchLooseWhitespace, BuiltinArgument::None, "",
                "match context lines ignoring changes in blank runs"},
            {'m', "merge", kBuiltinNotTreated, BuiltinArgument::Optional, "STYLE", ""},
            {'n', "normal", kPatchNormal, BuiltinArgument::None, "",
                "take the patch as a normal diff (it names no file)"},
            {'N', "forward", kPatchForward, BuiltinArgument::None, "",
                "ignore patches that seem to be applied already"},
            {'o', "output", kPatchOutput, BuiltinArgument::Required, "FILE",
                "put every file's result into FILE, in the order of the patch"},
            {'p', "strip", kPatchStrip, BuiltinArgument::Required, "NUM",
                "strip NUM leading components from the patch's file names"},
            {'r', "reject-file", kPatchRejectFile, BuiltinArgument::Required, "FILE",
                "put every reject into FILE ('-' throws them away)"},
            {'R', "reverse", kPatchReverse, BuiltinArgument::None, "",
                "take the patch as made with its old and new sides swapped"},
            {'s', "silent", kPatchSilent, BuiltinArgument::None, "",
                "work quietly, saying only what asks a question or sums up"},
            {0, "quiet", kPatchSilent, BuiltinArgument::None, "",
                "work quietly, saying only what asks a question or sums up"},
            {'t', "batch", kPatchBatch, BuiltinArgument::None, "",
                "do not ask questions; take the answers patch would assume"},
            {'u', "unified", kPatchUnified, BuiltinArgument::None, "",
                "take the patch as a unified diff"},
            {'v', "", kBuiltinOptionVersion, BuiltinArgument::None, "",
                "output version information"},
            {'V', "version-control", kPatchVersionControl, BuiltinArgument::Required, "METHOD",
                "keep backups as METHOD says: simple, numbered or existing"},
            {'x', "debug", kBuiltinNotTreated, BuiltinArgument::Required, "NUM", ""},
            {'Y', "basename-prefix", kBuiltinNotTreated, BuiltinArgument::Required, "PREFIX",
                ""},
            {'z', "suffix", kPatchSuffix, BuiltinArgument::Required, "SUFFIX",
                "back the original up as NAME.SUFFIX instead of NAME.orig"},
            {'Z', "set-utc", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {'T', "set-time", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "backup-if-mismatch", kPatchBackupIfMismatch, BuiltinArgument::None, "",
                "back the original up when a patch applies with fuzz or fails"},
            {0, "no-backup-if-mismatch", kPatchNoBackupIfMismatch, BuiltinArgument::None, "",
                "leave the original alone when a patch applies with fuzz or fails"},
            {0, "binary", kPatchBinary, BuiltinArgument::None, "",
                "keep the patch's carriage returns"},
            {0, "dry-run", kPatchDryRun, BuiltinArgument::None, "",
                "report what would be done, without changing any file"},
            {0, "reject-format", kPatchRejectFormat, BuiltinArgument::Required, "FORMAT",
                "write rejects in FORMAT: context or unified"},
            {0, "verbose", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "posix", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "quoting-style", kBuiltinNotTreated, BuiltinArgument::Required, "WORD", ""},
            {0, "read-only", kBuiltinNotTreated, BuiltinArgument::Required, "BEHAVIOR", ""},
            {0, "follow-symlinks", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "apply a diff file to an original",
            {"patch [OPTION]... [ORIGFILE [PATCHFILE]]"},
            "Every question is printed with its answer taken, as patch does when its\n"
            "input and output are not terminals: nothing is read back. Unified,\n"
            "context and normal diffs are read; a hunk is matched with up to -F\n"
            "context lines dropped (the side holding more of them loses first)\n"
            "and, with -l, its blank runs taken loosely. Ed scripts are not read;\n"
            "the answers questions assume are patch's no-terminal ones.",
        };
    }

    int Run(BuiltinContext& context) override;
};

std::shared_ptr<IBuiltinCommand> CreatePatchCommand() {
    return std::make_shared<PatchCommand>();
}

int PatchCommand::Run(BuiltinContext& context) {
    // patch prefixes its own Try line, so the shared BeginBuiltin does not
    // fit; its usage errors are all exit 2.
    const auto parsed = ParseBuiltinArgs(context.Args(), Options());
    const auto usageError = [&](const std::string& message) {
        context.Error(message);
        context.Error("Try 'patch --help' for more information.");
        return 2;
    };
    if (!parsed.error.empty()) {
        return usageError(parsed.error);
    }
    for (const auto& option : parsed.options) {
        if (option.id == kBuiltinOptionHelp) {
            context.Out(BuiltinHelpText(*this));
            return 0;
        }
        if (option.id == kBuiltinOptionVersion) {
            context.Out(BuiltinVersionText(*this));
            return 0;
        }
    }
    context.ReportNotTreated(parsed);

    PatchRun run(context);
    std::optional<std::string> input;
    int64_t strip = -1;
    std::optional<PatchFormat> forcedFormat;  // -u, -c or -n
    bool normalGiven = false;                 // -n (a normal diff names no file)
    std::optional<std::string> versionControl;  // the last -V word
    std::optional<std::string> suffixOption;     // the last -z SUFFIX
    for (const auto& option : parsed.options) {
        switch (option.id) {
            case kPatchDirectory: break;  // changed to below, before everything else
            case kPatchRemoveEmpty: run.removeEmpty = true; break;
            case kPatchForce: run.force = true; break;
            case kPatchForward: run.forward = true; break;
            case kPatchInput: input = option.argument; break;
            case kPatchOutput: run.outputFile = option.argument; break;
            case kPatchReverse: run.reverse = true; break;
            case kPatchSilent: run.silent = true; break;
            case kPatchBatch: run.batch = true; break;
            case kPatchUnified: forcedFormat = PatchFormat::Unified; break;
            case kPatchContext: forcedFormat = PatchFormat::Context; break;
            case kPatchNormal: forcedFormat = PatchFormat::Normal; normalGiven = true; break;
            case kPatchDryRun: run.dryRun = true; break;
            case kPatchBinary: run.binary = true; break;
            case kPatchLooseWhitespace: run.looseWhitespace = true; break;
            case kPatchBackup: run.backup = true; break;
            case kPatchBackupIfMismatch: run.mismatchBackup = true; break;
            case kPatchNoBackupIfMismatch: run.mismatchBackup = false; break;
            case kPatchRejectFile: run.rejectFile = option.argument; break;
            case kPatchRejectFormat:
                if (option.argument == "context") {
                    run.rejectFormat = RejFormat::Context;
                } else if (option.argument == "unified") {
                    run.rejectFormat = RejFormat::Unified;
                } else {
                    // This one carries the Try line on its own, prefixed
                    // as every message, and nothing else.
                    context.Error("Try 'patch --help' for more information.");
                    return 2;
                }
                break;
            case kPatchVersionControl: versionControl = option.argument; break;
            case kPatchSuffix:
                if (option.argument.empty()) {
                    context.Error("**** backup suffix is empty");
                    return 2;
                }
                suffixOption = option.argument;
                break;
            case kPatchStrip: {
                if (!ParseCountOption(context, "strip count", option.argument, strip)) {
                    return 2;
                }
                run.stripGiven = true;
                break;
            }
            case kPatchFuzz: {
                int64_t fuzz = 0;
                if (!ParseCountOption(context, "fuzz factor", option.argument, fuzz)) {
                    return 2;
                }
                run.fuzz = fuzz;
                break;
            }
        }
    }

    if (parsed.operands.size() > 2) {
        return usageError(parsed.operands[2] + ": extra operand");
    }
    if (!parsed.operands.empty()) {
        run.origOperand = parsed.operands[0];
    }

    // The backup method, decided once the options are parsed: the last -V
    // word, else PATCH_VERSION_CONTROL, else VERSION_CONTROL -- each read
    // only when set at all, an empty value meaning existing -- else
    // existing -- checked even when no backup will be made. GNU 2.7.6
    // takes none and off as numbered.
    {
        std::optional<std::string> word;
        const char* reportedName = "--version-control or -V option";
        if (versionControl) {
            word = versionControl;
        } else if (auto env = context.Process().GetEnvironment()
                                 ->GetVariable("PATCH_VERSION_CONTROL"); env) {
            if (!env->empty()) {
                word = env;
                reportedName = "$PATCH_VERSION_CONTROL";
            }
        } else if (auto env = context.Process().GetEnvironment()
                                 ->GetVariable("VERSION_CONTROL");
                   env && !env->empty()) {
            word = env;
            reportedName = "$VERSION_CONTROL";
        }
        BackupMode mode = BackupMode::Existing;
        if (word && !ParseBackupControlNamed(context, reportedName, *word, mode, false)) {
            return 2;
        }
        run.backupMode = mode == BackupMode::None ? BackupMode::Numbered : mode;
    }
    // The backup suffix: the last -z, else SIMPLE_BACKUP_SUFFIX (empty gives
    // the default), else .orig.
    if (suffixOption) {
        run.backupSuffix = *suffixOption;
    } else if (auto env = context.Process().GetEnvironment()
                             ->GetVariable("SIMPLE_BACKUP_SUFFIX");
               env && !env->empty()) {
        run.backupSuffix = *env;
    } else {
        run.backupSuffix = ".orig";
    }

    // -d first, as patch does: everything after happens in that directory.
    for (const auto& option : parsed.options) {
        if (option.id == kPatchDirectory && context.IO().ChangeDirectory(option.argument) != 0) {
            context.Error("**** Can't change to directory "
                + ShellEscapeQuoted(option.argument) + " : No such file or directory");
            return 2;
        }
    }

    // The -o file is truncated once, before any file is patched.
    if (run.outputFile && !run.dryRun) {
        run.outFile = context.IO().OpenFile(*run.outputFile, kFileOpenWriteCreateTruncate,
            kFileCreateMode);
        if (!run.outFile) {
            context.Error("**** Can't create file " + ShellEscapeQuoted(*run.outputFile)
                + " : " + CreateFailedReason(run, *run.outputFile));
            return 2;
        }
    }

    // The patch text: the PATCHFILE operand (which wins over -i FILE, before
    // or after it, as GNU patch's), or -i FILE, else the standard input.
    std::string patchText;
    {
        std::string patchName = "-";
        if (parsed.operands.size() >= 2) {
            patchName = parsed.operands[1];
        } else if (input) {
            patchName = *input;
        }
        InputOpenFailure failure = InputOpenFailure::None;
        auto file = OpenInputOperand(context, patchName, failure);
        if (!file) {
            context.Error("**** Can't open patch file " + ShellEscapeQuoted(patchName) + " : "
                + OpenFailureText(failure));
            return 2;
        }
        const WholeReadOutcome outcome = ReadWholeInput(context, *file, patchText);
        if (outcome == WholeReadOutcome::Stopped) {
            return 2;  // a quiet stop
        }
        if (outcome == WholeReadOutcome::Error) {
            context.Error("**** Can't open patch file " + ShellEscapeQuoted(patchName)
                + " : Input/output error");
            return 2;
        }
    }

    PatchReader reader(std::move(patchText), run.binary, forcedFormat,
        run.origOperand.has_value() || normalGiven);
    bool garbage = false;
    while (const std::optional<FilePatch> filePatch = reader.Next(strip, garbage)) {
        const int code = ApplyFilePatch(run, std::move(*filePatch));
        if (code != 0) {
            return code;
        }
        if (context.StopRequested()) {
            return 2;  // a quiet stop
        }
    }
    if (garbage) {
        context.Error("**** Only garbage was found in the patch input.");
        return 2;
    }
    return run.status;
}

} // namespace Haisos