#pragma once
#include <memory>
#include <optional>
#include <string>
#include "BuiltinCommand.h"
#include "commands/diff/DiffEngine.h"
#include "commands/diff/DiffOutput.h"

namespace Haisos {

// Everything one comparison of two files is run with, as the option parsing
// decides it. diff--diff-recursive builds one per pair of files it meets.
struct DiffSettings {
    DiffAnalysisOptions analysis;
    DiffOutputOptions output;
    bool brief = false;                 // -q
    bool reportIdentical = false;       // -s
    bool text = false;                  // -a
    bool stripTrailingCr = false;       // --strip-trailing-cr
    std::optional<std::string> label0, label1;
};

// Compares two non-directory operands -- "-" is standard input -- and prints
// the result; returns 0 same, 1 different, 2 trouble (already reported).
// |header|, when not empty, is printed (with a newline) before any output
// for this pair -- the recursive form's "diff -r a/x b/x" line.
int DiffTwoFiles(BuiltinContext& context, const DiffSettings& settings,
                 const std::string& name0, const std::string& name1, const std::string& header);

std::shared_ptr<IBuiltinCommand> CreateDiffCommand();

} // namespace Haisos