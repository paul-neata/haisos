#pragma once

#include <cstddef>

namespace Haisos::Unicode {

// The largest Unicode code point.
constexpr char32_t kMaxCodePoint = 0x10FFFF;

enum class DecodeStatus {
    Ok,          // a whole, valid character
    Invalid,     // the first byte starts no valid sequence: skip it alone
    Incomplete,  // a valid start, but the data ends before the sequence does
};

struct DecodedChar {
    DecodeStatus status = DecodeStatus::Invalid;
    char32_t codePoint = 0;  // meaningful only when status is Ok
    size_t length = 0;       // bytes taken: the sequence's (Ok), 1 (Invalid), 0 (Incomplete)
};

// Decodes the UTF-8 character at the start of data[0..size). size must be
// at least 1. Strict UTF-8 (RFC 3629): overlong forms, surrogates
// (U+D800-U+DFFF), code points above U+10FFFF, stray continuation bytes,
// C0, C1, F5-FF, and a sequence cut short by a byte that is not a
// continuation byte are all Invalid, with length 1 -- the caller skips
// that one byte and decodes again from the next (as glibc's mbrtowc
// returning -1). Incomplete only when every byte present is a valid
// prefix and the data simply ends.
DecodedChar DecodeUtf8(const char* data, size_t size);

// Whether c is printable (glibc's iswprint in a UTF-8 locale, compactly):
// every code point up to kMaxCodePoint except U+0000-U+001F,
// U+007F-U+009F, U+2028, U+2029, U+FDD0-U+FDEF, U+D800-U+DFFF and
// U+xxFFFE/U+xxFFFF of every plane. Unassigned code points count as
// printable (glibc says not).
bool IsPrintable(char32_t c);

// Whether c is white space (glibc's iswspace): U+0009-U+000D, U+0020,
// U+1680, U+2000-U+2006, U+2008-U+200A, U+2028, U+2029, U+205F, U+3000.
bool IsSpace(char32_t c);

// Whether c is one of the no-break spaces GNU wc also treats as word
// separators: U+00A0, U+2007, U+202F, U+2060.
bool IsNoBreakSpace(char32_t c);

// Columns c takes on a terminal (wcwidth): -1 if it is not printable
// (IsPrintable false), 0 for combining and format characters, 2 for East
// Asian wide and fullwidth characters and emoji, 1 otherwise. A compact
// table: rarer scripts' combining marks count 1.
int DisplayWidth(char32_t c);

} // namespace Haisos::Unicode
