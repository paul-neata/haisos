#pragma once

#include <string>

namespace Haisos::Hsh {

// A value as dash's `set`, `export -p` and `readonly -p` print it (dash's
// single_quote): in single quotes, each run of single quotes written as a
// double-quoted run.
// "plain" -> 'plain', "" -> '', "it's" -> 'it'"'"'s', "'x" -> ''"'"'x',
// "x'" -> 'x'"'", "''" -> ''"''".
std::string ShellSingleQuote(const std::string& value);

} // namespace Haisos::Hsh
