#pragma once
#include <optional>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "commands/awk/AwkError.h"

namespace Haisos::Awk {

// The option ids of the treated options (the not-treated ones share
// kBuiltinNotTreated; --help and --version are kBuiltinOptionHelp/Version).
enum AwkOptionId {
    kAwkOptionFieldSeparator = 1,
    kAwkOptionFile,
    kAwkOptionAssign,
    kAwkOptionHelp,
    kAwkOptionVersion,
    kAwkOptionPosix,
    kAwkOptionReInterval,
};

// Every option gawk accepts, treated or not; gawk reads the command line
// GNU-style: no option after the program operand.
const std::vector<BuiltinOption>& AwkOptionTable();

// A -v var=value or -F fs, applied before the program runs (parsed in later
// tasks). |fieldSeparator| tells -F from -v; the text is "var=value" as given
// (-F's as well), its escapes still undecoded.
struct AwkPreAssignment {
    bool fieldSeparator = false;
    std::string text;
};

// gawk's command line, parsed: -F and -v assignments in the order given, the
// -f files in the order given, the program operand (without -f) or nullopt,
// and the operands left: files and var=value assignments, which the
// interpreter (a later task) tells apart.
struct AwkInvocation {
    std::vector<AwkPreAssignment> preAssignments;
    std::vector<std::string> programFiles;  // each -f file, "-" standard input
    std::optional<std::string> programText;  // the program operand, without -f
    std::vector<std::string> operands;       // after the program operand
};

// gawk's two usage lines, each on its own newline ("Usage: awk ..." twice).
std::string AwkUsageText();

// Parses the command line: --help and --version print their text (status 0),
// a usage error gawk's (status 1: the invalid-option word, then the two
// usage lines, on stderr), a -v argument without '=' gawk's word (status 1),
// not-treated options are reported and ignored. Options after the program
// operand are operands, as gawk reads them. Returns nullopt when the command
// is already done, with *exitStatus set.
std::optional<AwkInvocation> ParseAwkInvocation(
    BuiltinContext& context, const IBuiltinCommand& command, int& exitStatus);

// Reads the program: the -f files in order ("-" is standard input), or the
// program operand alone. A file that cannot be opened or read is gawk's
// fatal (status 2), a directory its own error (status 1); a stop asked for
// while reading ends quietly (status 143). Returns nullopt then, with
// *exitStatus set; each source is named as given on the command line.
std::optional<std::vector<AwkSource>> LoadAwkSources(
    BuiltinContext& context, const AwkInvocation& invocation, int& exitStatus);

} // namespace Haisos::Awk