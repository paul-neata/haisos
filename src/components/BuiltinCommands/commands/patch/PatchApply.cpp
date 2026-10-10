#include "PatchApply.h"
#include <algorithm>
#include <cstdint>

namespace Haisos {
namespace {

// The hunk's old lines: its Context and Delete lines, in order.
std::vector<const PatchLine*> HunkOldLines(const PatchHunk& hunk) {
    std::vector<const PatchLine*> oldLines;
    for (const PatchLine& line : hunk.lines) {
        if (line.kind != PatchLineKind::Insert) {
            oldLines.push_back(&line);
        }
    }
    return oldLines;
}

// How many context lines each side of the hunk |fuzz| drops: the side with
// more context loses them first, so unbalanced context keeps its balance as
// long as it can (a 3-1 hunk at fuzz 2 loses 2 leading lines and none
// trailing; at fuzz 3 it loses 3 and 1).
struct FuzzSplit {
    int64_t leadingIgnored = 0;
    int64_t trailingIgnored = 0;
};

FuzzSplit SplitFuzz(const PatchHunk& hunk, int64_t fuzz) {
    FuzzSplit split;
    if (fuzz <= 0) {
        return split;
    }
    const int64_t leading = hunk.leadingContext;
    const int64_t trailing = hunk.trailingContext;
    if (leading >= trailing) {
        split.leadingIgnored = std::min(fuzz, leading);
        split.trailingIgnored = std::min(std::max<int64_t>(0, fuzz - (leading - trailing)), trailing);
    } else {
        split.trailingIgnored = std::min(fuzz, trailing);
        split.leadingIgnored = std::min(std::max<int64_t>(0, fuzz - (trailing - leading)), leading);
    }
    return split;
}

// The tokens of a line's first |end| bytes: runs of blanks (space, tab)
// separate them, so "  b" is ["", "b"] while "b" is ["b"] -- a blank run
// matches nothing at a line's start or inside it.
std::vector<std::string> BlankTokens(const std::string& text, size_t end) {
    std::vector<std::string> tokens;
    std::string token;
    for (size_t i = 0; i < end; ++i) {
        if (text[i] == ' ' || text[i] == '\t') {
            tokens.push_back(token);
            token.clear();
            while (i < end && (text[i] == ' ' || text[i] == '\t')) {
                ++i;
            }
            --i;
        } else {
            token += text[i];
        }
    }
    tokens.push_back(token);
    return tokens;
}

// Whether the hunk's old lines [first, last) equal the file's lines from
// |at| (1-based, the hunk's first old line) on, byte for byte or under -l.
bool HunkMatchesAt(const PatchTarget& target, const std::vector<const PatchLine*>& oldLines,
                   int64_t at, size_t first, size_t last, bool loose) {
    for (size_t i = first; i < last; ++i) {
        const std::string& fileLine =
            target.lines[static_cast<size_t>(at + static_cast<int64_t>(i) - 1)];
        const std::string& patchLine = oldLines[i]->text;
        if (loose ? !LooseLineEqual(fileLine, patchLine) : fileLine != patchLine) {
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

// One range of a context-format .rej hunk: "0" for a side with no lines
// (whatever its stated start), "S" for one line, else "S,E".
std::string RejContextRange(int64_t start, int64_t count) {
    if (count == 0) {
        return "0";
    }
    if (count == 1) {
        return std::to_string(start);
    }
    return std::to_string(start) + "," + std::to_string(start + count - 1);
}

} // namespace

int64_t LocateHunk(const PatchTarget& target, const PatchHunk& hunk,
                   const LocateOptions& options, int64_t consumedLines,
                   int64_t& runningOffset) {
    const std::vector<const PatchLine*> oldLines = HunkOldLines(hunk);
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

    const FuzzSplit split = SplitFuzz(hunk, options.fuzz);
    const size_t first = static_cast<size_t>(split.leadingIgnored);
    const size_t last = oldLines.size() - static_cast<size_t>(split.trailingIgnored);
    // A hunk whose context is unbalanced is anchored: with less leading than
    // trailing context and a place at the start of the file, only line 1;
    // with less trailing than leading context, only the file's end.
    const int64_t leading = hunk.leadingContext - split.leadingIgnored;
    const int64_t trailing = hunk.trailingContext - split.trailingIgnored;
    const bool startAnchored = leading < trailing && hunk.oldStart <= 1;
    const bool endAnchored = trailing < leading;

    // The candidates: the hunk's first old line (ignored ones included) at
    // L >= 1, its first compared line (L + the ignored leading ones) not
    // before the last line the earlier hunks of this file used up (fuzz
    // cannot reach back into it), and its ground reaching past that line
    // (a hunk matching wholly inside it would garble the output); the
    // compared lines lie inside the file, the ignored trailing ones may
    // run past its end.
    const int64_t firstCandidate = std::max<int64_t>(1,
        std::max(consumedLines - split.leadingIgnored,
                 consumedLines - oldCount + 2));
    const int64_t lastCandidate = fileLines - oldCount + split.trailingIgnored + 1;
    const int64_t endPosition = lastCandidate;
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
            if (first >= last  // nothing left to compare: every candidate matches
                || HunkMatchesAt(target, oldLines, candidate, first, last,
                                 options.looseWhitespace)) {
                runningOffset = candidate - hunk.oldStart;
                return candidate;
            }
        }
    }
    return 0;
}

bool LooseLineEqual(const std::string& a, const std::string& b) {
    // Blanks at the end of a line, and its newline, are ignored.
    const auto endOf = [](const std::string& text) {
        size_t end = text.size();
        while (end > 0 && (text[end - 1] == ' ' || text[end - 1] == '\t'
            || text[end - 1] == '\n')) {
            --end;
        }
        return end;
    };
    return BlankTokens(a, endOf(a)) == BlankTokens(b, endOf(b));
}

std::string RejText(const FilePatch& patch, const std::vector<RejectedHunk>& rejected,
                    RejFormat format) {
    if (rejected.empty()) {
        return std::string();
    }
    std::string out;
    const auto header = [&out](const char* mark, const std::optional<std::string>& name,
                               const std::string& timeText) {
        out += mark;
        out += ' ';
        if (name) {
            out += *name;
            if (!timeText.empty()) {
                out += '\t';
                out += timeText;
            }
        } else {
            out += "/dev/null";  // no time text on a /dev/null side
        }
        out += '\n';
    };
    if (format == RejFormat::Unified) {
        header("---", patch.oldName, patch.oldTimeText);
        header("+++", patch.newName, patch.newTimeText);
        for (const RejectedHunk& rejection : rejected) {
            const PatchHunk& hunk = patch.hunks[rejection.number - 1];
            out += "@@ -" + RejRange(hunk.oldStart + rejection.lineShift, hunk.oldCount)
                + " +" + RejRange(hunk.newStart + rejection.lineShift, hunk.newCount) + " @@";
            if (!hunk.function.empty()) {
                out += ' ';
                out += hunk.function;
            }
            out += '\n';
            for (const PatchLine& line : hunk.lines) {
                out += static_cast<char>(line.kind);
                out += line.text;
            }
        }
        return out;
    }
    // The context format: the old-style suffixes (no " ****", a " -----")
    // for an old-style context or a normal diff, the new-style ones
    // otherwise; a normal diff's marks never include "! ". Each hunk holds
    // its two parts: the old lines (Context and Delete) under its "***"
    // range, the new ones (Context and Insert) under its "---" range, a
    // context line in both.
    const bool oldStyle = patch.format == PatchFormat::OldContext
        || patch.format == PatchFormat::Normal;
    const bool allowBang = patch.format != PatchFormat::Normal;
    header("***", patch.oldName, patch.oldTimeText);
    header("---", patch.newName, patch.newTimeText);
    // The marks: context "  "; a run of Delete lines directly followed by a
    // run of Insert lines is a change ("! " on both, when this format writes
    // them); any other Delete "- ", Insert "+ ".
    const auto markFor = [allowBang](const std::vector<PatchLine>& lines, size_t at) {
        const PatchLineKind kind = lines[at].kind;
        if (kind == PatchLineKind::Context) {
            return "  ";
        }
        if (kind == PatchLineKind::Delete) {
            size_t end = at + 1;
            while (end < lines.size() && lines[end].kind == PatchLineKind::Delete) {
                ++end;
            }
            const bool change = allowBang && end < lines.size()
                && lines[end].kind == PatchLineKind::Insert;
            return change ? "! " : "- ";
        }
        size_t start = at;  // back over this Insert run: a Delete run
        while (start > 0 && lines[start - 1].kind == PatchLineKind::Insert) {
            --start;
        }
        const bool change = allowBang && start > 0
            && lines[start - 1].kind == PatchLineKind::Delete;
        return change ? "! " : "+ ";
    };
    for (const RejectedHunk& rejection : rejected) {
        const PatchHunk& hunk = patch.hunks[rejection.number - 1];
        out += "***************";
        if (!hunk.function.empty()) {
            out += ' ';
            out += hunk.function;
        }
        out += '\n';
        const int64_t shift = rejection.lineShift;
        out += "*** " + RejContextRange(hunk.oldStart + shift, hunk.oldCount);
        out += oldStyle ? "\n" : " ****\n";
        const auto writePart = [&](bool oldSide) {
            for (size_t i = 0; i < hunk.lines.size(); ++i) {
                const PatchLine& line = hunk.lines[i];
                // A context line belongs to both parts; a Delete to the old
                // one, an Insert to the new.
                if (oldSide ? line.kind == PatchLineKind::Insert
                            : line.kind == PatchLineKind::Delete) {
                    continue;
                }
                out += markFor(hunk.lines, i);
                out += line.text;  // a noNewline line ends without a '\n',
                // and no "\ No newline" line follows it
            }
        };
        writePart(true);
        out += "--- " + RejContextRange(hunk.newStart + shift, hunk.newCount);
        out += oldStyle ? " -----\n" : " ----\n";
        writePart(false);
    }
    return out;
}

} // namespace Haisos