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
// for this pair -- the recursive form's "diff -r a/x b/x" line. A side
// flagged |missing0|/|missing1| (diff -N against a file that is not there)
// is read as empty bytes and shows the epoch time in a header.
int DiffTwoFiles(BuiltinContext& context, const DiffSettings& settings,
                 const std::string& name0, const std::string& name1, const std::string& header,
                 bool missing0 = false, bool missing1 = false);

// The long option |name| names: an exact match over the whole table first,
// else an unambiguous prefix of one, the way the shared parser finds it too.
const BuiltinOption* DiffFindLongOption(const std::string& name,
                                        const std::vector<BuiltinOption>& options);

std::shared_ptr<IBuiltinCommand> CreateDiffCommand();

} // namespace Haisos