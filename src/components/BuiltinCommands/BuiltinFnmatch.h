#pragma once
#include <string_view>

namespace Haisos {

// glibc's fnmatch() flags, same values.
constexpr int kFnmPathname = 1;    // '*', '?' and brackets never match '/'
constexpr int kFnmNoEscape = 2;    // '\' is an ordinary character
constexpr int kFnmPeriod = 4;      // a leading '.' (at the start, or after '/' with kFnmPathname) only matches a literal '.'
constexpr int kFnmLeadingDir = 8;  // also match when the pattern matches a prefix of text followed by '/'
constexpr int kFnmCaseFold = 16;   // ASCII letters match either case

// Whether |text| as a whole matches the shell pattern |pattern|, as glibc's
// fnmatch(pattern, text, flags) == 0 in the C locale.
bool FnMatch(std::string_view pattern, std::string_view text, int flags = 0);

} // namespace Haisos