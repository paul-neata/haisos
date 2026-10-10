#include "commands/jq/JqPrelude.h"

#include <cassert>

namespace Haisos::Jq {

const std::string& StandardPrelude() {
    static const std::string prelude =
        "def select(f): if f then . else empty end;\n"
        "def recurse(f): ., (f | recurse(f));\n"
        ".";
    return prelude;
}

const ParseResult& ParsedPrelude() {
    static const ParseResult parsed = ParseProgram(StandardPrelude());
    assert(parsed.errors.empty() && parsed.root &&
           "the prelude is a fixed program and always parses");
    return parsed;
}

} // namespace Haisos::Jq