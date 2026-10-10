#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace Haisos {

// GNU's number-with-suffix grammar, the size-taking builtins share it (du's
// -B and -t, cmp's -i and -n; head/tail are to reuse it too).

enum class SizeParse { Ok, Invalid, InvalidSuffix, Overflow };

// gnulib xstrtoumax, base 10: optional leading blanks, digits (none: the
// value is 1 when what follows is a valid suffix, else Invalid; a '-' sign is
// Invalid), then at most one suffix from |validSuffixes|. Suffix values:
// b 512, c 1, w 2, k K 1024, m M 1024^2, G 1024^3, T, P, E, Z, Y, R, Q the
// next powers of 1024 (only the case listed in validSuffixes is accepted).
// When validSuffixes contains '0', a suffix may be followed by "iB" (still
// 1024-based: "KiB" is 1024) or "B" / "D" (1000-based: "KB" and "kB" are
// 1000, "MB" 10^6). Anything left after that: InvalidSuffix. A value too big
// for uintmax_t: Overflow, and |out| is the maximum.
SizeParse ParseSizeWithSuffix(std::string_view text, std::string_view validSuffixes, uintmax_t& out);

// GNU's human-readable size (-h: powers of 1024, units K M G T P E Z Y; --si:
// powers of 1000, units k M G T ...), rounded up, one decimal below 10:
// 0, 1023, 1.0K, 8.0K, 20K, 1.5M; with |si| 8.2k, 21k. Below one unit, the
// plain number.
std::string FormatHumanSize(uint64_t bytes, bool si);

} // namespace Haisos