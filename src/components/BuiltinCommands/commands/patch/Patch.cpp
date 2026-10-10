#include <sys/stat.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
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
    std::optional<std::string> outputFile;   // -o FILE
    std::optional<std::string> origOperand;  // the ORIGFILE operand
    int status = 0;  // 1 when a hunk failed or was ignored, a file patch was
    // skipped, or a deletion was refused
    std::set<std::string> origMade;    // targets whose .orig was written
    std::set<std::string> rejWritten;  // .rej files written this run
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

// Writes the .rej text: replaced by the first rejects of a run, appended to by
// later ones.
void WriteRejFile(PatchRun& run, const std::string& rejName, const std::string& rejText) {
    if (rejText.empty()) {
        return;
    }
    const int flags = run.rejWritten.count(rejName) ? kFileOpenWriteCreateAppend
                                                    : kFileOpenWriteCreateTruncate;
    run.rejWritten.insert(rejName);
    auto file = run.context.IO().OpenFile(rejName, flags, kFileCreateMode);
    if (!file) {
        return;
    }
    WriteFully(*file, rejText);
}

// The whole block of a patch that names no file to patch: the text leading up
// to it, and (with -f or -t) the notice, else the questions nobody reads.
void NoFileBlock(PatchRun& run, const FilePatch& patch) {
    if (!run.silent) {
        run.Out("can't find file to patch at input line "
            + std::to_string(patch.reportLine) + "\n");
        run.Out(run.stripGiven ? "Perhaps you used the wrong -p or --strip option?\n"
                               : "Perhaps you should have used the -p or --strip option?\n");
    }
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

    // A git rename or copy reads one file and writes another. Its OLD is
    // oldName and its NEW newName, whichever way round -R left them.
    bool isRenameOrCopy = patch.gitRename || patch.gitCopy;
    bool removeOldAfter = false;
    std::string input;   // the file read
    std::string output;  // the file written
    std::string patchingSuffix;
    if (isRenameOrCopy && patch.oldName && patch.newName) {
        input = *patch.oldName;
        output = *patch.newName;
        const bool oldExists = run.Exists(input);
        if (oldExists) {
            patchingSuffix = patch.gitRename ? " (renamed from " : " (copied from ";
            patchingSuffix += ShellEscapeQuoted(input) + ")";
            removeOldAfter = patch.gitRename;
        } else if (patch.gitCopy) {
            // A copy needs both files: with the source gone there is nothing
            // to read and nothing to write.
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
            line += ShellEscapeQuoted(*run.outputFile) + " (read from " + ShellEscapeQuoted(input) + ")";
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
    } else if (patch.oldAbsence == SideAbsence::Present && !run.force) {
        // No creation, and nothing forced: the file a normal patch needs.
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
    size_t written = 0;  // input lines already copied into the result
    std::vector<std::string> result;
    std::vector<size_t> rejHunks;  // 1-based numbers of the failed/ignored
    bool skipped = false;          // the whole file patch was skipped
    bool anyOffset = false;
    for (size_t h = 0; h < total; ++h) {
        if (run.context.StopRequested()) {
            return 2;  // a quiet stop
        }
        PatchHunk& hunk = patch.hunks[h];
        if (skipped) {
            rejHunks.push_back(h + 1);
            continue;
        }
        const int64_t expected = hunk.oldStart + runningOffset;
        int64_t at = creationRefused ? 0 : LocateHunk(target, hunk, 0, consumedLines,
                                                      runningOffset);
        // Hunk #1 alone, when it fits nowhere: perhaps the patch was already
        // applied and its sides want swapping.
        if (h == 0 && at == 0 && !run.force && !decided && !creationRefused) {
            SwapFilePatch(patch);
            int64_t probeOffset = 0;
            const int64_t probe = LocateHunk(target, patch.hunks[0], 0, 0, probeOffset);
            if (probe != 0) {
                const char* detected = run.reverse ? "Unreversed patch detected!"
                                                  : "Reversed (or previously applied) patch detected!";
                const PatchAnswer answer = AskReversal(run, detected, run.reverse);
                if (answer == PatchAnswer::Skip) {
                    SwapFilePatch(patch);  // back as given, for the .rej
                    skipped = true;
                    rejHunks.push_back(1);
                    continue;
                }
                decided = true;  // kept swapped, as the answer asked
                at = probe;
                runningOffset = probeOffset;
            } else {
                SwapFilePatch(patch);
            }
        }
        if (at == 0) {
            rejHunks.push_back(h + 1);
            if (!run.silent) {
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
        if (at != hunk.oldStart && FirstOldLine(hunk) != nullptr) {
            anyOffset = true;
            if (!run.silent) {
                const int64_t offset = at - hunk.oldStart;
                run.Out("Hunk #" + std::to_string(h + 1) + " succeeded at "
                    + std::to_string(at) + " (offset " + std::to_string(offset)
                    + (offset == 1 ? " line" : " lines") + ").\n");
            }
        }
        // The input lines before the hunk, then the hunk up to its last
        // change; its trailing context waits for what copies the rest.
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
                    result.push_back(target.lines[index]);
                    ++index;
                    break;
                case PatchLineKind::Delete:
                    ++index;
                    break;
                case PatchLineKind::Insert:
                    result.push_back(hunk.lines[i].text);
                    break;
            }
        }
        written = index;
        consumedLines = at + hunk.oldCount - hunk.trailingContext - 1;
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

    // The rejects, and the line that says where they went.
    if (!rejHunks.empty()) {
        const std::string rejName = run.outputFile ? *run.outputFile + ".rej" : output + ".rej";
        PrintSummary(run, rejHunks.size(), total, skipped, rejName);
        if (!run.dryRun) {
            WriteRejFile(run, rejName, RejText(patch, rejHunks));
        }
    }

    // A patch skipped at its question writes nothing: the file stands as it
    // was, and no .orig of it is kept.
    if (skipped) {
        return 0;
    }

    // The output. Nothing is written under --dry-run; -o collects every
    // result in one file; otherwise a temporary next to the target is renamed
    // into place.
    if (!run.dryRun) {
        if (!run.outputFile && (anyOffset || !rejHunks.empty())) {
            const std::string origPath = input + ".orig";
            const std::string key = run.context.IO().ResolvePath(origPath);
            if (run.origMade.insert(key).second) {
                auto orig = run.context.IO().OpenFile(origPath, kFileOpenWriteCreateTruncate,
                    kFileCreateMode);
                if (orig) {
                    WriteFully(*orig, inputBytes);
                }
            }
        }
        if (run.outputFile) {
            if (run.outFile) {
                WriteFully(*run.outFile, content);
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
            if (run.context.IO().Rename(tmp, output) != 0) {
                run.context.IO().RemoveFile(tmp);
                run.Fatal("Can't create file " + ShellEscapeQuoted(output)
                    + " : No such file or directory");
                return 2;
            }
        }
        if (removeOldAfter && !run.dryRun) {
            run.context.IO().RemoveFile(input);
        }
        if (!run.outputFile) {
            if (ResultRemoved(patch, content, run)) {
                run.context.IO().RemoveFile(output);
            } else if (patch.newAbsence == SideAbsence::Surely && !content.empty()) {
                if (!run.silent) {
                    run.Out("Not deleting file " + ShellEscapeQuoted(output)
                        + " as content differs from patch\n");
                }
                run.status = 1;
            }
        }
    }
    return 0;
}

} // namespace

class PatchCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "patch"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'b', "backup", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {'B', "prefix", kBuiltinNotTreated, BuiltinArgument::Required, "PREFIX", ""},
            {'c', "context", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {'d', "directory", kPatchDirectory, BuiltinArgument::Required, "DIR",
                "read and write files in directory DIR"},
            {'D', "ifdef", kBuiltinNotTreated, BuiltinArgument::Required, "NAME", ""},
            {'e', "ed", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {'E', "remove-empty-files", kPatchRemoveEmpty, BuiltinArgument::None, "",
                "remove files left empty by patching"},
            {'f', "force", kPatchForce, BuiltinArgument::None, "",
                "do not ask questions; take every patch as it comes"},
            {'F', "fuzz", kBuiltinNotTreated, BuiltinArgument::Required, "NUM", ""},
            {'g', "get", kBuiltinNotTreated, BuiltinArgument::Required, "NUM", ""},
            {'i', "input", kPatchInput, BuiltinArgument::Required, "FILE",
                "read the patch from FILE ('-' the standard input)"},
            {'l', "ignore-whitespace", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {'m', "merge", kBuiltinNotTreated, BuiltinArgument::Optional, "STYLE", ""},
            {'n', "normal", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {'N', "forward", kPatchForward, BuiltinArgument::None, "",
                "ignore patches that seem to be applied already"},
            {'o', "output", kPatchOutput, BuiltinArgument::Required, "FILE",
                "put every file's result into FILE, in the order of the patch"},
            {'p', "strip", kPatchStrip, BuiltinArgument::Required, "NUM",
                "strip NUM leading components from the patch's file names"},
            {'r', "reject-file", kBuiltinNotTreated, BuiltinArgument::Required, "FILE", ""},
            {'R', "reverse", kPatchReverse, BuiltinArgument::None, "",
                "take the patch as made with its old and new sides swapped"},
            {'s', "silent", kPatchSilent, BuiltinArgument::None, "",
                "work quietly, saying only what asks a question or sums up"},
            {0, "quiet", kPatchSilent, BuiltinArgument::None, "",
                "work quietly, saying only what asks a question or sums up"},
            {'t', "batch", kPatchBatch, BuiltinArgument::None, "",
                "do not ask questions; take the answers patch would assume"},
            {'u', "unified", kPatchUnified, BuiltinArgument::None, "",
                "unified diffs, the only format read"},
            {'v', "", kBuiltinOptionVersion, BuiltinArgument::None, "",
                "output version information"},
            {'V', "version-control", kBuiltinNotTreated, BuiltinArgument::Required, "METHOD",
                ""},
            {'x', "debug", kBuiltinNotTreated, BuiltinArgument::Required, "NUM", ""},
            {'Y', "basename-prefix", kBuiltinNotTreated, BuiltinArgument::Required, "PREFIX",
                ""},
            {'z', "suffix", kBuiltinNotTreated, BuiltinArgument::Required, "SUFFIX", ""},
            {'Z', "set-utc", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {'T', "set-time", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "backup-if-mismatch", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "no-backup-if-mismatch", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "binary", kPatchBinary, BuiltinArgument::None, "",
                "keep the patch's carriage returns"},
            {0, "dry-run", kPatchDryRun, BuiltinArgument::None, "",
                "report what would be done, without changing any file"},
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
            "input and output are not terminals: nothing is read back. Only unified\n"
            "diffs are read, and a hunk applies only where its lines match the file\n"
            "exactly; the fuzz factor, backup files and the other diff formats are\n"
            "not treated yet.",
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
            case kPatchUnified: break;  // the only format read
            case kPatchDryRun: run.dryRun = true; break;
            case kPatchBinary: run.binary = true; break;
            case kPatchStrip: {
                bool ok = !option.argument.empty();
                bool negative = false;
                size_t i = 0;
                if (!option.argument.empty() && option.argument[0] == '-') {
                    negative = true;
                    i = 1;
                }
                int64_t value = 0;
                for (; ok && i < option.argument.size(); ++i) {
                    const char c = option.argument[i];
                    if (c < '0' || c > '9') {
                        ok = false;
                        break;
                    }
                    if (value > (INT64_MAX - (c - '0')) / 10) {
                        ok = false;
                        break;
                    }
                    value = value * 10 + (c - '0');
                }
                if (!ok) {
                    context.Error("**** strip count " + ShellEscapeQuoted(option.argument)
                        + " is not a number");
                    return 2;
                }
                if (negative) {
                    context.Error("**** strip count " + ShellEscapeQuoted(option.argument)
                        + " is negative");
                    return 2;
                }
                strip = value;
                run.stripGiven = true;
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
                + " : No such file or directory");
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

    PatchReader reader(std::move(patchText), run.binary);
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