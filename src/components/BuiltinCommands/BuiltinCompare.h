#pragma once
#include <string_view>

namespace Haisos {

// GNU sort -n in the C locale (gnulib strnumcmp, decimal point '.', no
// thousands separator), on keys whose leading blanks the caller has already
// skipped. Negative, zero or positive, as memcmp.
//
// A number is an optional '-', digits, optionally '.' and digits; parsing
// stops at the first byte that does not fit (so '+1', the ',' of '1,000' and
// 'abc' all contribute nothing: they are 0). '-0', '-', '-.', '0', '0.000'
// and '' are all equal. Compare: by sign first (a negative non-zero < zero <
// positive); two positives by integer part with leading zeros ignored (more
// significant digits = larger, then digit by digit), then by fraction digit
// by digit with trailing zeros ignored; two negatives the same, reversed.
// Never converts to a floating type: numbers of any length compare exactly.
int CompareNumeric(std::string_view a, std::string_view b);

// sort -g: each text as strtold() reads it (on a NUL-terminated copy;
// strtold skips leading whitespace itself and accepts inf, nan, hex floats
// and exponents). A text strtold cannot convert at all (end == start) is
// "not a number". Order: not-a-number < NaN < numbers by value (-0 == +0);
// two not-a-numbers are equal, two NaNs are equal.
int CompareGeneralNumeric(std::string_view a, std::string_view b);

// sort -h (leading blanks skipped by the caller): first by unit order, then
// CompareNumeric. Unit order of a text: after an optional '-', walk digits,
// then an optional '.' and digits; if any digit walked is non-zero, the
// order is that of the byte right after the number -- K or k 1, M 2, G 3,
// T 4, P 5, E 6, Z 7, Y 8, R 9, Q 10, anything else 0 -- negated when the
// '-' was there; a number with no non-zero digit has order 0.
int CompareHumanNumeric(std::string_view a, std::string_view b);

// sort -M: each text's month -- after skipping leading blanks, its first
// three bytes compared case-insensitively (ASCII) with JAN FEB MAR APR MAY
// JUN JUL AUG SEP OCT NOV DEC: 1..12, else 0 -- compared as numbers.
int CompareMonth(std::string_view a, std::string_view b);

// GNU's version order (gnulib filevercmp), as sort -V and ls -v use it.
// Equal texts equal; an empty text sorts before any other; texts starting
// with '.' sort before the rest ('.' first, then '..'), their version-like
// suffixes (.tar.gz) cut before the numbers compare, longer digit runs
// larger, '~' before an end of text.
int CompareVersion(std::string_view a, std::string_view b);

} // namespace Haisos