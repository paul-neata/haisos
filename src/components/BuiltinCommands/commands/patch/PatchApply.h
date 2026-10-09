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

// Where |hunk| applies in |target|: the 1-based line its old lines start at,
// or 0 if nowhere. |fuzz| context lines may be ignored at each end
// (diff--patch-core always passes 0). |consumedLines|: the lines earlier
// hunks of this file have used up. |runningOffset|: the offset the earlier
// hunks were found at; updated to this hunk's offset when it is found.
int64_t LocateHunk(const PatchTarget& target, const PatchHunk& hunk, int64_t fuzz,
                   int64_t consumedLines, int64_t& runningOffset);

// The .rej text of |patch|'s hunks |hunkNumbers| (1-based, in order): the
// ---/+++ header once, then each hunk in unified form, its lines with their
// marks and no "\ No newline" lines.
std::string RejText(const FilePatch& patch, const std::vector<size_t>& hunkNumbers);

} // namespace Haisos