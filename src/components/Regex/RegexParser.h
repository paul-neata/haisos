#pragma once
#include <string>
#include <string_view>

#include "Regex.h"
#include "RegexTree.h"

namespace Haisos {

// Parses |pattern| in the syntax |options.syntax| says into |tree|. Returns
// true with a complete tree; false with |error| holding the message GNU
// (glibc for Basic/Extended, PCRE2 for Perl) prints for that mistake -- the
// tree's contents are then unspecified. Group numbers are by the position of
// the opening parenthesis, left to right.
bool ParseRegex(std::string_view pattern, const RegexOptions& options, RegexTree& tree, std::string& error);

}