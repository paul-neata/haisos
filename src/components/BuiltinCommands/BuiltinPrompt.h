#pragma once
#include <optional>
#include <string>
#include "BuiltinCommand.h"
#include "src/components/libheaders/DescriptorLineReader.h"

namespace Haisos {

// GNU's yesno(): writes |question| to standard error exactly as given (no
// newline added), reads one line from the process's standard input, and
// answers true only when that line's first byte is 'y' or 'Y' (" y" and an
// empty line are no). End of input, a failed read, and a stop while waiting
// are all "no". One instance per run of a command, so lines read ahead are
// kept for the next question.
class BuiltinPrompt {
public:
    explicit BuiltinPrompt(BuiltinContext& context);
    bool Ask(const std::string& question);

private:
    BuiltinContext& m_context;
    // Made on the first Ask, over the process's standard input.
    std::optional<DescriptorLineReader> m_reader;
};

} // namespace Haisos