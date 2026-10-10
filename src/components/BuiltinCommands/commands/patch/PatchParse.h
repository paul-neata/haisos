#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Haisos {

// One body line of a hunk, with its mark. |text| keeps the '\n' unless the
// line is followed by a "\ No newline at end of file" marker.
enum class PatchLineKind : char { Context = ' ', Delete = '-', Insert = '+' };

struct PatchLine {
    PatchLineKind kind;
    std::string text;        // the line, '\n' included unless |noNewline|
    bool noNewline = false;  // followed by "\ No newline at end of file"
};

struct PatchHunk {
    // The ranges of the "@@ -A,B +C,D @@" line. oldStart is the first old
    // line the hunk covers: A, or A + 1 when B is 0 (an empty range "-A,0"
    // means "after line A"); likewise newStart.
    int64_t oldStart = 0, oldCount = 0;
    int64_t newStart = 0, newCount = 0;
    std::vector<PatchLine> lines;  // in the patch's order
    std::string function;          // what follows the second "@@" (for the .rej)
    int64_t leadingContext = 0, trailingContext = 0;  // context lines before
    // the first / after the last change
    int64_t headerLine = 0;  // input line number of its "@@" line
};

// Which diff format a patch is written in. OldContext is a context diff
// without the " ****" range suffixes; Normal is "A[,B]cCdD[,E]".
enum class PatchFormat { Unified, Context, OldContext, Normal };

// How sure the patch is that one side's file does not exist.
enum class SideAbsence { Present, Maybe, Surely };

// One file's worth of a patch: its headers and hunks.
struct FilePatch {
    PatchFormat format = PatchFormat::Unified;
    std::optional<std::string> oldName, newName, indexName;  // after -p;
    // nullopt: /dev/null, or too few components
    std::string oldTimeText, newTimeText;  // the rest of the ---/+++ line
    // after the name, '\n' and '\r' removed
    SideAbsence oldAbsence = SideAbsence::Present;
    SideAbsence newAbsence = SideAbsence::Present;
    bool gitDiff = false, gitRename = false, gitCopy = false;
    bool stripTrailingCr = false;
    int64_t reportLine = 0;  // the line "can't find file to patch at input
    // line N" names
    std::string leadingText;  // the input lines from the end of the previous
    // patch up to the hunks
    std::vector<PatchHunk> hunks;  // the well-formed hunks, in order
    std::string malformed;         // non-empty: the fatal message met after
    // |hunks|
    std::string malformedBeforeFile;  // a context diff's first hunk malformed:
    // reported before anything else of this file patch
};

// A whole patch text split into file patches. A plain class over the text
// it was built with, nothing else.
class PatchReader {
public:
    // |format| says which format the options allow: unified, context (both
    // styles), normal, or any of them (nullopt) -- but a normal diff is
    // recognised only when |normalAllowed| is set (an ORIGFILE operand or -n
    // names its file).
    PatchReader(std::string text, bool binary, std::optional<PatchFormat> format,
                bool normalAllowed);

    // One line of the input: its text without the '\n', and whether the
    // input ended it with one (the last line may not).
    struct InputLine {
        std::string text;
        bool delimited = true;
    };

    // The next file patch, or nullopt at the end of the input. |strip| is
    // -p (-1: not given). Sets |garbage| when the input is not empty and
    // holds no patch at all.
    std::optional<FilePatch> Next(int64_t strip, bool& garbage);

private:
    // The line |index| holds for the parser: without its trailing '\r' when
    // this patch's lines carry one and --binary was not given.
    std::string LineText(size_t index, bool& stripCr) const;

    // The "@@ -A[,B] +C[,D] @@[ FUNC]" line at |index|, when it is one.
    // |tooLarge| is filled with the raw number a start or count names when it
    // holds more digits than an int64 can, and |overflow| set when a start's
    // empty range ("A,0", start A + 1) would overflow: both are hunk lines
    // the reader reports as fatal when it reaches them.
    bool ParseHunkHeader(const std::string& text, int64_t& oldStart, int64_t& oldCount,
                         int64_t& newStart, int64_t& newCount, std::string& function,
                         std::string* tooLarge, bool* overflow) const;

    // One hunk, its "@@" line at |index|: appended to |patch|'s hunks, or its
    // |malformed| message set. Returns the index of the first line after it
    // (the input's end when a truncated hunk had to be completed or refused).
    size_t ReadHunk(size_t index, FilePatch& patch);

    // One context-diff hunk, its "***************" line at |index|: appended
    // to |patch|'s hunks, or its |malformed| message set. Returns the index of
    // the first line after it.
    size_t ReadContextHunk(size_t index, FilePatch& patch);

    // One normal-diff hunk, its "A[,B]{a|c|d}C[,D]" line at |index|: appended
    // to |patch|'s hunks, or its |malformed| message set. Returns the index of
    // the first line after it.
    size_t ReadNormalHunk(size_t index, FilePatch& patch);

    std::vector<InputLine> m_lines;
    size_t m_pos = 0;      // where the next patch's leading text starts
    size_t m_lastEnd = 0;  // the line after the previous patch's last line
    bool m_binary;
    std::optional<PatchFormat> m_format;  // which format the options allow
    bool m_normalAllowed = false;         // a normal diff names a file
};

// -R, and the answer "yes, it is reversed": swaps old and new (names, time
// texts, absences, each hunk's ranges, Delete <-> Insert; within each run of
// changed lines the deletions come first, as in any unified diff).
void SwapFilePatch(FilePatch& patch);

} // namespace Haisos