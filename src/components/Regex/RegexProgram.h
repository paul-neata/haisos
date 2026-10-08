#pragma once
#include <bitset>
#include <cstddef>
#include <string>
#include <vector>

#include "Regex.h"
#include "RegexTree.h"

namespace Haisos {

enum class RegexOp {
    Byte,   // x: index into RegexProgram::byteSets; consumes one byte of the set
    Split,  // try x first, then y
    Jump,   // x: target
    Save,   // x: capture slot; write the current position into it
    Assert, // assertion: holds or fails without consuming
    BackRef, // x: group; match the bytes its slot pair holds again
    ProgressMark,   // x: mark slot; write the current position into it
    ProgressCheck,  // x: mark slot; jump to y when it holds the current position
    Match,
};

struct RegexInstruction {
    RegexOp op = RegexOp::Match;
    int x = 0;  // Byte: index into byteSets; Split: preferred target; Jump: target;
                // Save: capture slot; BackRef: group; ProgressMark/ProgressCheck: mark slot
    int y = 0;  // Split: the other target; ProgressCheck: the loop's exit
    RegexAssertion assertion = RegexAssertion::LineStart;  // Assert
    bool multiline = false;   // Assert
    bool ignoreCase = false; // BackRef
};

constexpr size_t kMaxRegexInstructions = 1000000;

struct RegexProgram {
    std::vector<RegexInstruction> instructions;  // starts at 0
    std::vector<std::bitset<256>> byteSets;
    size_t slotCount = 0;   // 2 * (groupCount + 1): slot 2k start, 2k+1 end of group k
    size_t markCount = 0;   // ProgressMark slots
    bool longest = true;    // Basic/Extended: leftmost-longest; Perl: leftmost-first
    bool hasBackReferences = false;
    std::bitset<256> firstBytes;   // bytes that can be consumed first
    bool canSkip = false;          // true when every match consumes one of firstBytes first
    bool anchoredAtTextStart = false;  // the pattern begins with \` or a non-multiline ^
};

// Compiles |tree| into |program|; false with |error| set when the program
// would exceed kMaxRegexInstructions.
bool CompileRegexProgram(const RegexTree& tree, RegexSyntax syntax, RegexProgram& program, std::string& error);

// Whether an Assert instruction holds at position |p| of |text|, with |flags|
// (kRegexNotBol/kRegexNotEol). Shared by the two engines.
bool RegexAssertionHolds(const RegexInstruction& instruction, std::string_view text, size_t p, int flags);

// Both: the leftmost match at or after |start|, in the program's mode; on
// success |slots| (size slotCount) holds the offsets, -1 for unset.
bool PikeSearch(const RegexProgram& program, std::string_view text, size_t start, int flags, std::vector<std::ptrdiff_t>& slots);
bool BacktrackSearch(const RegexProgram& program, std::string_view text, size_t start, int flags, std::vector<std::ptrdiff_t>& slots);

} // namespace Haisos