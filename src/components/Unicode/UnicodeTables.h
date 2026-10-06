#pragma once

// The width tables behind DisplayWidth(), declared here (not in Unicode.h)
// so the unit tests can check them: each array is sorted and, within
// itself, disjoint.

#include <cstddef>

namespace Haisos::Unicode::Tables {

struct CodePointRange {
    char32_t first;
    char32_t last;
};

// Combining and format characters: DisplayWidth 0 when printable.
extern const CodePointRange kZeroWidth[];
extern const size_t kZeroWidthCount;

// East Asian wide and fullwidth characters and emoji: DisplayWidth 2.
// Contains some ranges of kZeroWidth (e.g. U+302A-U+302D inside
// U+2E80-U+303E); the zero-width table is consulted first and wins.
extern const CodePointRange kWide[];
extern const size_t kWideCount;

// Whether c falls in one of the ranges (binary search; sorted, disjoint).
bool InRanges(const CodePointRange* ranges, size_t count, char32_t c);

} // namespace Haisos::Unicode::Tables
