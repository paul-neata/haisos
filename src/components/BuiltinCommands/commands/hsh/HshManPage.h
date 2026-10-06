#pragma once

#include <string>

namespace Haisos::Hsh {

// hsh's manual page, as `man hsh` prints it: plain text, section headings at
// column 0 in capitals, the body indented 7 spaces, no line over 79
// characters, a blank line between sections, ending in '\n'.
std::string HshManPage();

} // namespace Haisos::Hsh
