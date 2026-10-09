#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "src/components/Regex/Regex.h"

namespace Haisos::Sed {

// The compiled script, filled by ParseScript (SedParser.h) and run by
// RunScript (SedExecutor.h). The structures leave room for the commands of
// the advanced task (b t T a i c r R w W y l z F v e, s///w) in the same
// switches without a redesign.

enum class AddressKind {
    None,
    Line,           // N
    Last,           // $
    Regex,          // /RE/ or \cREc; a null regex is "//": the last regex used
    Step,           // first~step (step 0: first only)
    Zero,           // 0, of 0,/RE/
    RelativeLines,  // +N, as a second address
    Multiple,       // ~N, as a second address
};

struct Address {
    AddressKind kind = AddressKind::None;
    uint64_t line = 0;  // Line: the line; Step: first; RelativeLines/Multiple: N
    uint64_t step = 0;  // Step: the step
    bool ignoreCase = false;
    bool multiline = false;
    std::shared_ptr<const Regex> regex;
};

// The parsed right-hand side of s: pieces of literal text, group references
// (0 for &) and the case conversion commands \L \U \l \u \E.
struct ReplacementPiece {
    enum class Kind { Text, Group, UpperRest, LowerRest, UpperNext, LowerNext, EndCase };
    Kind kind = Kind::Text;
    std::string text;  // Text
    int group = 0;    // Group
};

struct Replacement {
    std::vector<ReplacementPiece> pieces;
};

struct Command {
    Address a1, a2;
    bool negate = false;   // addr!
    char name = 0;         // 's', 'd', 'p', 'n', 'q', 'Q', '=', ':', '{', '}'
    // s: the regex (null: the last regex used), the replacement and flags.
    std::shared_ptr<const Regex> regex;
    std::shared_ptr<Replacement> replacement;
    bool global = false;
    bool print = false;      // s's p
    uint64_t occurrence = 1;  // s///N
    std::string text;        // : label
    int intArg = 0;          // q/Q exit code
    size_t jump = 0;         // '{': index after the matching '}'
    // Runtime state of a range address, carried in the script (a script
    // object serves one run).
    bool rangeActive = false;
    bool rangeEnded = false;   // a 0,/re/ past its end, never restarting
    uint64_t rangeStart = 0;   // the line the range started on
};

struct Script {
    std::vector<Command> commands;
    bool quietFromScript = false;  // "#n" as the script's first two characters
    // The labels, for b/t/T (search--sed-advanced): name -> command index.
    std::vector<std::pair<std::string, size_t>> labels;
    // Where a runtime script error is reported (GNU reports the position at
    // the end of the parse): the last piece read.
    bool errorLocationIsFile = false;
    std::string errorFileName;
    int errorLine = 1;        // for a file piece
    int errorExpression = 1;  // for an -e piece
};

} // namespace Haisos::Sed