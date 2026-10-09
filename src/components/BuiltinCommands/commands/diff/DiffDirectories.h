#pragma once
#include <optional>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "commands/diff/Diff.h"

namespace Haisos {

// Everything one directory comparison is run with, as the option parsing of
// diff--diff-recursive decides it.
struct DiffTreeSettings {
    bool recursive = false;                 // -r
    bool newFile = false;                   // -N
    bool unidirectionalNewFile = false;     // --unidirectional-new-file
    std::vector<std::string> excludes;      // -x patterns and -X file lines
    std::optional<std::string> startingFile;  // -S
    // The options echoed in the "diff OPTIONS A B" lines, each word as typed
    // and shell-quoted, each preceded by a space; empty without options.
    std::string switchString;
};

// Compares two operands as given on the command line -- files, directories,
// "-" (standard input), a missing one (with -N/--unidirectional-new-file) --
// and prints everything; returns the exit status 0 same, 1 different,
// 2 trouble (already reported).
int DiffOperands(BuiltinContext& context, const DiffSettings& settings,
                 const DiffTreeSettings& tree, const std::string& name0, const std::string& name1);

} // namespace Haisos