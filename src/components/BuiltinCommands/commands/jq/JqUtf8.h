#pragma once

#include <string>
#include <string_view>

namespace Haisos::Jq {

// Appends |codePoint| to |out| as UTF-8.
void AppendUtf8(std::string& out, unsigned int codePoint);

// jq's handling of bytes that are not valid UTF-8 (JSON strings, -R lines,
// --arg values, --rawfile contents): every ill-formed piece becomes one
// U+FFFD, everything valid is kept. A byte that can start no sequence
// (80-BF, C0, C1, F5-FF) is one U+FFFD on its own; a lead byte C2-F4 takes
// the continuation bytes that follow it, up to the 1, 2 or 3 it needs --
// all there, a whole character (an overlong form, a surrogate or a code
// point above U+10FFFF becomes one U+FFFD for the whole sequence); cut
// short by a byte that is not a continuation or by the end, one U+FFFD
// covers the lead and the continuation bytes taken, and the next byte is
// read afresh.
std::string RepairUtf8(std::string_view bytes);

// The number of code points in |utf8|: a byte that starts no sequence
// (or one cut short by the end) counts as one, as RepairUtf8 makes each
// such piece one U+FFFD.
size_t Utf8Length(const std::string& utf8);

// The byte offset where the |codePoints|'th code point of |utf8| starts,
// clamped to utf8.size(). Slices of strings count code points, so a
// from/to pair is translated here once before the bytes are taken.
size_t Utf8ByteOffset(const std::string& utf8, size_t codePoints);

} // namespace Haisos::Jq