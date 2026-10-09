#pragma once
#include <memory>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "SedScript.h"
#include "interfaces/IFileDescriptor.h"

namespace Haisos::Sed {

struct ExecSettings {
    bool quiet = false;       // -n
    bool separate = false;    // -s: the files are separate streams
    char delimiter = '\n';   // -z: '\0'
    int lineLength = 70;      // -l: l's wrap length (0 never wraps)
    bool unbuffered = false;  // -u
    // -i: where all output goes instead of standard output (a temp file the
    // caller renames over the original); "w /dev/stdout" still writes to the
    // real standard output.
    std::shared_ptr<IFileDescriptor> inPlaceOutput;
};

// Runs |script| over the input operands ("-" for standard input) and returns
// the exit status: GNU's 4 for a read error, 2 when an input could not be
// opened, q/Q's code, else 0. The range-address state in |script| is reset
// (each call is one run), the w/W file state (open files, pending newlines)
// is carried in it.
int RunScript(BuiltinContext& context, Script& script, const std::vector<std::string>& inputs,
              const ExecSettings& settings);

} // namespace Haisos::Sed