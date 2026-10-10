#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include "BuiltinCommand.h"
#include "commands/grep/GrepContext.h"
#include "commands/grep/GrepMatcher.h"
#include "commands/grep/GrepSettings.h"
#include "interfaces/IFileDescriptor.h"

namespace Haisos {

struct GrepFileResult {
    uint64_t selected = 0;  // how many lines this input selected
    bool error = false;     // a diagnostic was reported for this input
    bool stopped = false;   // -q found a match, or the process was asked to stop
};

// Searches one input and prints what the settings ask for. |shownName| is
// the name printed (the operand, or the label for standard input);
// |sizeForTab| the file's size when known (regular file), for -T.
// |grepContext|: the run's before/after context, when it was asked for and
// the output is lines (not -c -l -L -q) -- the context spans the whole run
// (a group separator also goes between groups of different files), and this
// input joins it with BeginFile; null when no context is in effect.
GrepFileResult GrepOneInput(BuiltinContext& context, const GrepSettings& settings, const GrepMatcher& matcher,
                            IFileDescriptor& input, const std::string& shownName,
                            std::optional<uint64_t> sizeForTab, GrepContext* grepContext);

} // namespace Haisos