#pragma once

#include <string>

#include "commands/jq/JqParser.h"

namespace Haisos::Jq {

// The prelude: the library jq's own manual defines in the language itself,
// written here from the manual (nothing of jq's implementation). It ends
// with the program's own root, so one parse covers both; ParsedPrelude
// keeps the result (parsed once, the tree built for each program that
// compiles over it).
const std::string& StandardPrelude();
const ParseResult& ParsedPrelude();

} // namespace Haisos::Jq