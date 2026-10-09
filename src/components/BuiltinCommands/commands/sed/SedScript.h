#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "interfaces/IFileDescriptor.h"
#include "src/components/Regex/Regex.h"

namespace Haisos::Sed {

// The compiled script, filled by ParseScript (SedParser.h) and run by
// RunScript (SedExecutor.h). The structures cover the whole of GNU sed 4.9
// but the `e` command (which HaisosOS does not treat).

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

// b/t/T to the end of the script (an empty label), as a jump target.
constexpr size_t kJumpToEnd = static_cast<size_t>(-1);

// One w/W/s///w target, opened while the script is parsed (GNU reports a
// file it cannot open there, exiting 4 without reading any input). Each
// target keeps its own pending-newline state across everything written to
// it, as GNU's open file does.
struct OutputFile {
    std::string name;
    bool isStdout = false;  // "w /dev/stdout": standard output even under -i
    bool isStderr = false;  // "w /dev/stderr"
    std::shared_ptr<IFileDescriptor> file;
    bool missing = false;  // a record was written without its delimiter
};

struct Command {
    Address a1, a2;
    bool negate = false;   // addr!
    char name = 0;         // 's', 'd', 'p', 'n', 'q', 'Q', '=', ':', '{', '}', ...
    // s: the regex (null: the last regex used), the replacement and flags.
    std::shared_ptr<const Regex> regex;
    std::shared_ptr<Replacement> replacement;
    bool global = false;
    bool print = false;      // s's p
    uint64_t occurrence = 1;  // s///N
    std::string text;        // a/i/c: the text; b/t/T and ':': the label;
                             // r/R: the file name; e: the command line
    std::string text2;       // y: the destination string (text holds the source)
    int intArg = 0;          // q/Q exit code
    int wFile = -1;          // w/W/s///w: an index into Script::outputFiles
    int lLength = -1;        // l: its own wrap length (settings' otherwise)
    bool noText = false;     // a/i/c: the classic `x\` form ended the script
                             // before any text line was read (a and i do
                             // nothing, c discards the pattern but writes
                             // nothing)
    size_t jump = 0;         // '{': index after the matching '}'; b/t/T: the
                             // label's index, or kJumpToEnd for an empty label
    // Runtime state of a range address, carried in the script (a script
    // object serves one run).
    bool rangeActive = false;
    bool rangeEnded = false;   // a 0,/re/ past its end, never restarting
    uint64_t rangeStart = 0;   // the line the range started on
};

struct Script {
    std::vector<Command> commands;
    bool quietFromScript = false;  // "#n" as the script's first two characters
    // The labels, for b/t/T: name -> command index.
    std::vector<std::pair<std::string, size_t>> labels;
    // The w/W/s///w targets, in the order the script named them.
    std::vector<OutputFile> outputFiles;
    // The status of a parse failure: 1 for a bad script, 4 for a file that
    // could not be opened or a label a jump cannot find.
    int parseErrorStatus = 1;
    // Where a runtime script error is reported (GNU reports the position at
    // the end of the parse): the last piece read.
    bool errorLocationIsFile = false;
    std::string errorFileName;
    int errorLine = 1;        // for a file piece
    int errorExpression = 1;  // for an -e piece
};

} // namespace Haisos::Sed