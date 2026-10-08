#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "commands/grep/GrepMatcher.h"

namespace Haisos {

enum class GrepListFiles { None, Matching, NonMatching };  // -l / -L
enum class GrepBinaryFiles { Binary, Text, WithoutMatch }; // --binary-files
enum class GrepDirectories { Read, Recurse, Skip };        // -d
// GNU's default -D value: devices named on the command line are read, those
// met while recursing are skipped.
enum class GrepDevices { ReadCommandLine, Read, Skip };

// One --include/--exclude pattern, in the order given (the list is walked from
// the last to the first, the first match deciding).
struct GrepFilter {
    std::string pattern;
    bool include = false;
};

// --color/--colour's WHEN.
enum class GrepColorWhen { Never, Always, Auto };

// GREP_COLORS' capabilities, with GNU's defaults.
struct GrepColors {
    std::string ms = "01;31";  // a match in a selected line
    std::string mc = "01;31";  // a match in a context line
    std::string sl;            // a selected line's whole text
    std::string cx;            // a context line's whole text
    std::string fn = "35";     // the file name
    std::string ln = "32";     // the line number
    std::string bn = "32";     // the byte offset
    std::string se = "36";     // the ':', '-' and group separators
    bool rv = false;           // with -v, swap sl and cx
    bool ne = false;           // no ESC [ K (clear-to-end-of-line) bytes
};

// Everything the options decide: matcher options and patterns, and what
// GrepOneInput prints. Grep.cpp resolves withFilename to a value once it
// knows the operand count.
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
    // --- search--grep-recursive ---
    GrepDirectories directories = GrepDirectories::Read;  // -d (and -r/-R: Recurse)
    GrepDevices devices = GrepDevices::ReadCommandLine;    // -D
    std::vector<GrepFilter> filters;  // --include/--exclude/--exclude-from, in order
    std::vector<std::string> excludeDirs;  // --exclude-dir patterns
    size_t before = 0;                // -B
    size_t after = 0;                 // -A
    bool contextEnabled = false;       // any of -A/-B/-C/-NUM given, even 0
    std::optional<std::string> groupSeparator = "--";  // nullopt: --no-group-separator
    bool nullAfterName = false;        // -Z
    GrepColorWhen colorWhen = GrepColorWhen::Never;
    bool color = false;                // colorWhen resolved against the output
    GrepColors colors;
};

} // namespace Haisos