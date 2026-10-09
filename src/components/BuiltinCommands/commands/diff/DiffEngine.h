#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Haisos {

// How lines are compared: one mode (the strongest given wins, see Diff.cpp).
enum class DiffWhiteSpace { None, TabExpansion, TrailingSpace, SpaceChange, AllSpace };

struct DiffLineOptions {
    DiffWhiteSpace whiteSpace = DiffWhiteSpace::None;
    bool ignoreCase = false;
    size_t tabSize = 8;
};

// One file's text: its bytes split into lines. lineStarts has lineCount + 1
// entries (the last is bytes.size()); line i is bytes[lineStarts[i],
// lineStarts[i+1]), its '\n' included -- except an incomplete last line.
struct DiffText {
    std::string bytes;
    std::vector<size_t> lineStarts;
    bool missingNewline = false;   // the last line has no '\n'
    size_t LineCount() const { return lineStarts.size() - 1; }
};

// Splits |bytes| into lines; with stripTrailingCr, every "\r\n" becomes "\n"
// first. When !robustOutput (ed), a missing final newline is appended
// (missingNewline still set, for the warning).
DiffText MakeDiffText(std::string bytes, bool stripTrailingCr, bool robustOutput);

// One change: |deleted| lines of file 0 from line0, |inserted| lines of
// file 1 from line1 (0-based). With deleted == 0, line0 is the line before
// which the insertion goes; likewise line1 with inserted == 0.
struct DiffChange { int64_t line0, line1, deleted, inserted; };

struct DiffAnalysisOptions {
    DiffLineOptions lines;
    int64_t horizonLines = 0;   // see ComputeDiff: the fixed common ends
    bool robustOutput = true;   // false for -e
};

// The changes, in file order, or empty when the files are the same under
// the options. |stop| is checked once per edit-distance step; when it
// becomes true the result is empty and |stopped| is set.
//
// The comparison is Myers' O(ND) algorithm in its linear-space divide and
// conquer form (the paper, section 4b), with the choices pinned down in
// DiffEngine.cpp so one fixed minimal script comes out (GNU's `diff -d`
// placement; see the notes in the builtin's --help).
std::vector<DiffChange> ComputeDiff(const DiffText& a, const DiffText& b,
                                    const DiffAnalysisOptions& options,
                                    const std::function<bool()>& stop, bool& stopped);

} // namespace Haisos