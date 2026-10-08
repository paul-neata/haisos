#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "commands/grep/GrepMatcher.h"

namespace Haisos {

enum class GrepListFiles { None, Matching, NonMatching };  // -l / -L
enum class GrepBinaryFiles { Binary, Text, WithoutMatch }; // --binary-files

// Everything the options decide: matcher options and patterns, and what
// GrepOneInput prints. Grep.cpp resolves withFilename to a value once it
// knows the operand count. search--grep-recursive adds its fields here.
struct GrepSettings {
    GrepMatcherOptions matcher;
    std::vector<std::string> patterns;  // one pattern per line of -e/-f/the operand
    bool invert = false;                // -v
    bool count = false;                 // -c
    GrepListFiles listFiles = GrepListFiles::None;
    bool quiet = false;                 // -q
    bool onlyMatching = false;           // -o
    bool lineNumbers = false;           // -n
    bool byteOffsets = false;           // -b
    std::optional<bool> withFilename;   // -H/-h; unset: by operand count
    std::string label = "(standard input)";  // --label: what standard input is shown as
    bool initialTab = false;            // -T
    std::optional<uint64_t> maxCount;   // -m; absent = unlimited; 0 = nothing selected
    bool noMessages = false;            // -s
    GrepBinaryFiles binaryFiles = GrepBinaryFiles::Binary;
    bool nullData = false;              // -z: lines end in a NUL byte
    bool lineBuffered = false;          // --line-buffered
};

} // namespace Haisos