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

} // namespace Haisos