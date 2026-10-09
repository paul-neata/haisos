#pragma once
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "SedScript.h"

namespace Haisos::Sed {

struct ExecSettings {
    bool quiet = false;       // -n
    bool separate = false;    // -s: the files are separate streams
};

// Runs |script| over the input operands ("-" for standard input) and returns
// the exit status: GNU's 4 for a read error, 2 when an input could not be
// opened, q/Q's code, else 0.
int RunScript(BuiltinContext& context, Script& script, const std::vector<std::string>& inputs,
              const ExecSettings& settings);

} // namespace Haisos::Sed