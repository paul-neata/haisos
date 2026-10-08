#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include "BuiltinCommand.h"
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
GrepFileResult GrepOneInput(BuiltinContext& context, const GrepSettings& settings, const GrepMatcher& matcher,
                            IFileDescriptor& input, const std::string& shownName,
                            std::optional<uint64_t> sizeForTab);

} // namespace Haisos