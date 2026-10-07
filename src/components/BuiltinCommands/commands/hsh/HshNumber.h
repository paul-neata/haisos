#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace Haisos::Hsh {

// dash's atomax10 (strtoimax's base-10 form): blanks, an optional sign and one
// or more decimal digits, blanks at the end. Anything else, a number out of
// intmax_t's range, or no digits at all is nullopt -- what dash's callers
// ("shift", "test") then report as "Illegal number: <text>".
std::optional<intmax_t> Atomax10(const std::string& text);

} // namespace Haisos::Hsh
