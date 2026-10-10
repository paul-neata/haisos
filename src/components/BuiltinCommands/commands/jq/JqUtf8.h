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

} // namespace Haisos::Jq