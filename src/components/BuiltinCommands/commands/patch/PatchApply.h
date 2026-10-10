#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "commands/patch/PatchParse.h"

namespace Haisos {

// The lines of the file a hunk is applied to: each with its '\n' (the last
// may lack it), in order.
struct PatchTarget {
    std::vector<std::string> lines;
};

// How a hunk is looked for: |fuzz| context lines may be dropped (the side
// with more context loses them first), |looseWhitespace| is -l.
struct LocateOptions {
    int64_t fuzz = 0;
    bool looseWhitespace = false;
};

// Where |hunk| applies in |target|: the 1-based line its old lines start at
// (the ignored context lines included), or 0 if nowhere. |consumedLines|:
// the lines earlier hunks of this file have used up -- the hunk's first
// compared line may not lie before the last of them. |runningOffset|: the
// offset the earlier hunks were found at; updated to this hunk's offset
// when it is found.
int64_t LocateHunk(const PatchTarget& target, const PatchHunk& hunk,
                   const LocateOptions& options, int64_t consumedLines,
                   int64_t& runningOffset);

// Whether two lines compare equal under -l: runs of blanks (space, tab)
// match whatever their length, blanks at the end of a line and its newline
// are ignored, and a run of blanks matches nothing at a line's start or in
// its middle.
bool LooseLineEqual(const std::string& a, const std::string& b);

// One rejected hunk: its 1-based number in |patch|, and the line shift of
// the hunks applied before it (its .rej ranges are written in the output's
// line numbers).
struct RejectedHunk {
    size_t number;
    int64_t lineShift;
};

// The format a .rej file is written in: the unified one, or the context
// one (what a context or normal diff's own rejects look like).
enum class RejFormat { Unified, Context };

// The .rej text of |patch|'s rejected |hunks|, in order: the header once,
// then each hunk in |format|, its lines with their marks and no "\ No
// newline" lines.
std::string RejText(const FilePatch& patch, const std::vector<RejectedHunk>& hunks,
                    RejFormat format);

} // namespace Haisos