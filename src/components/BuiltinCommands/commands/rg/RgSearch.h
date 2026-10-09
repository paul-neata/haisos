#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "commands/grep/GrepMatcher.h"

namespace Haisos {

// What rg prints: the output modes of ripgrep 14.1.1, one at a time (the
// last one given wins, --files included).
enum class RgMode {
    Lines,            // the default
    Count,            // -c: matching lines per file
    CountMatches,     // --count-matches (and -c -o): matches per file
    ListMatching,     // -l
    ListNonMatching,  // --files-without-match
    Quiet,            // -q
    Files,            // --files
};

// Everything the search itself needs, decided by Rg.cpp. The three
// std::optional overrides (-H/-I, --heading/--no-heading, -n/-N) fall back to
// ripgrep's terminal guesses, which only RgSearch can make: it sees the
// output stream.
struct RgSettings {
    RgMode mode = RgMode::Lines;
    bool invert = false;                    // -v
    bool onlyMatching = false;              // -o
    bool includeZero = false;               // --include-zero, with the counts
    std::optional<bool> withFilename;       // -H / -I
    std::optional<bool> heading;             // --heading / --no-heading
    std::optional<bool> lineNumbers;         // -n / -N
    bool columns = false;                    // --column
    bool byteOffsets = false;                // -b
    bool nullSeparator = false;              // -0
    std::optional<uint64_t> maxColumns;      // -M; 0 (given) means no limit
    std::optional<uint64_t> maxCount;        // -m; 0 (given) means search nothing
    size_t before = 0;                      // -B
    size_t after = 0;                       // -A
    bool contextEnabled = false;             // -A/-B/-C given, even 0
    bool contextSeparator = true;            // --no-context-separator
    std::string separator = "--";           // --context-separator
    bool text = false;                       // -a: no binary detection at all
    bool binary = false;                     // --binary: walked files too
    bool noMessages = false;                 // --no-messages
    int color = 1;                           // --color: 0 never, 1 auto, 2 always
    bool lineBuffered = false;                // --line-buffered
};

// What the run came to, for Rg.cpp to turn into an exit status.
struct RgResult {
    bool matched = false;          // a line matched, or a path was printed
    bool error = false;            // a diagnostic was reported
    bool stopped = false;          // -q matched, or the process was asked to stop
    bool quietMatched = false;     // -q found its match
    bool noFilesSearched = false;  // the implicit path, and nothing searched
};

// rg's walking, reading and printing: the operands are paths (none means
// standard input when it has data, else .); directories are walked depth
// first, each directory's entries in byte order. Everything a process
// reaches, it reaches through context.IO().
RgResult RgSearch(BuiltinContext& context, const RgSettings& settings,
                  const GrepMatcher& matcher, const std::vector<std::string>& paths);

} // namespace Haisos