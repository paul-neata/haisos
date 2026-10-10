#include "PatchApply.h"
#include <algorithm>

namespace Haisos {
namespace {

// The hunk's old lines (its Context and Delete lines), up to |fuzz| context
// lines dropped at each end.
std::vector<const PatchLine*> HunkOldLines(const PatchHunk& hunk, int64_t fuzz) {
    std::vector<const PatchLine*> oldLines;
    for (const PatchLine& line : hunk.lines) {
        if (line.kind != PatchLineKind::Insert) {
            oldLines.push_back(&line);
        }
    }
    size_t front = 0;
    while (front < static_cast<size_t>(std::max<int64_t>(0, fuzz))
        && front < oldLines.size() && oldLines[front]->kind == PatchLineKind::Context) {
        ++front;
    }
    size_t back = 0;
    while (back < static_cast<size_t>(std::max<int64_t>(0, fuzz))
        && back + front < oldLines.size()
        && oldLines[oldLines.size() - 1 - back]->kind == PatchLineKind::Context) {
        ++back;
    }
    if (front == 0 && back == 0) {
        return oldLines;
    }
    std::vector<const PatchLine*> kept(oldLines.begin() + front, oldLines.end() - back);
    return kept;
}

// Whether the hunk's old lines equal the file's lines from |at| (1-based) on,
// byte for byte, the missing final newline included.
bool HunkMatchesAt(const PatchTarget& target, const std::vector<const PatchLine*>& oldLines,
                   int64_t at) {
    for (size_t i = 0; i < oldLines.size(); ++i) {
        if (target.lines[static_cast<size_t>(at - 1) + i] != oldLines[i]->text) {
            return false;
        }
    }
    return true;
}

// One range of a .rej hunk header: "S" for a count of 1, "S-1,0" for 0 (an
// empty range "-A,0" names the line after A), else "S,count".
std::string RejRange(int64_t start, int64_t count) {
    if (count == 1) {
        return std::to_string(start);
    }
    if (count == 0) {
        return std::to_string(start - 1) + ",0";
    }
    return std::to_string(start) + "," + std::to_string(count);
}

} // namespace

int64_t LocateHunk(const PatchTarget& target, const PatchHunk& hunk, int64_t fuzz,
                   int64_t consumedLines, int64_t& runningOffset) {
    const std::vector<const PatchLine*> oldLines = HunkOldLines(hunk, fuzz);
    const int64_t oldCount = static_cast<int64_t>(oldLines.size());
    const int64_t fileLines = static_cast<int64_t>(target.lines.size());
    const int64_t expected = hunk.oldStart + runningOffset;

    // An insertion with no old lines is not compared: it goes where the patch
    // says, or at the end of a shorter file.
    if (oldCount == 0) {
        int64_t at = expected;
        if (at > fileLines + 1) {
            at = fileLines + 1;
        }
        if (at < 1) {
            at = 1;
        }
        runningOffset = at - hunk.oldStart;
        return at;
    }

    // A hunk whose context is unbalanced is anchored: with less leading than
    // trailing context and a place at the start of the file, only line 1;
    // with less trailing than leading context, only the file's end.
    const int64_t leading = std::max<int64_t>(0, hunk.leadingContext - fuzz);
    const int64_t trailing = std::max<int64_t>(0, hunk.trailingContext - fuzz);
    const bool startAnchored = leading < trailing && hunk.oldStart <= 1;
    const bool endAnchored = trailing < leading;

    // The candidates: L > consumedLines and L + oldLines - 1 <= fileLines.
    const int64_t firstCandidate = consumedLines + 1;
    const int64_t lastCandidate = fileLines - oldCount + 1;
    const int64_t endPosition = fileLines - oldCount + 1;
    if (lastCandidate < firstCandidate) {
        return 0;  // no place is left for the hunk's old lines
    }
    // Searching outward from the nearest candidate to the expected place
    // tries them in the same order as from the expected place itself, and a
    // stated line far beyond the file costs no empty steps.
    const int64_t origin = std::min(std::max(expected, firstCandidate), lastCandidate);
    for (int64_t distance = 0;; ++distance) {
        const int64_t forward = origin + distance;
        const int64_t backward = origin - distance;
        if (forward > lastCandidate && backward < firstCandidate) {
            break;  // no candidate is left, near or far
        }
        const int64_t tried[2] = {forward, backward};
        const int count = distance == 0 ? 1 : 2;  // forward and backward are the same
        for (int which = 0; which < count; ++which) {
            const int64_t candidate = tried[which];
            if (candidate < firstCandidate || candidate > lastCandidate) {
                continue;
            }
            if (startAnchored && candidate != 1) {
                continue;
            }
            if (endAnchored && candidate != endPosition) {
                continue;
            }
            if (HunkMatchesAt(target, oldLines, candidate)) {
                runningOffset = candidate - hunk.oldStart;
                return candidate;
            }
        }
    }
    return 0;
}

std::string RejText(const FilePatch& patch, const std::vector<size_t>& hunkNumbers) {
    if (hunkNumbers.empty()) {
        return std::string();
    }
    std::string out;
    const auto header = [&out](const char* mark, const std::optional<std::string>& name,
                               const std::string& timeText) {
        out += mark;
        out += ' ';
        out += name ? *name : "/dev/null";
        if (!timeText.empty()) {
            out += '\t';
            out += timeText;
        }
        out += '\n';
    };
    header("---", patch.oldName, patch.oldTimeText);
    header("+++", patch.newName, patch.newTimeText);
    for (const size_t number : hunkNumbers) {
        const PatchHunk& hunk = patch.hunks[number - 1];
        out += "@@ -" + RejRange(hunk.oldStart, hunk.oldCount)
            + " +" + RejRange(hunk.newStart, hunk.newCount) + " @@";
        if (!hunk.function.empty()) {
            out += ' ';
            out += hunk.function;
        }
        out += '\n';
        for (const PatchLine& line : hunk.lines) {
            out += static_cast<char>(line.kind);
            out += line.text;  // a noNewline line ends without a '\n', and no
            // "\ No newline" line follows it
        }
    }
    return out;
}

} // namespace Haisos