#pragma once

#include <string>

#include "commands/jq/JqValue.h"

namespace Haisos::Jq {

// How WriteJson lays a value out. `indent` spaces per level (a `tab` each
// instead when set); 0 with no tab is jq's compact one-line form.
// `sortKeys` writes every object's members in key order. `ascii` escapes
// every non-ASCII character as \uXXXX (a surrogate pair above the BMP).
// `color` writes jq's own ANSI colours.
struct WriteOptions {
    int indent = 2;
    bool tab = false;
    bool sortKeys = false;
    bool ascii = false;
    bool color = false;
};

// Appends |value| as JSON text to |out|, byte for byte as jq writes it
// (its pretty layout, its number text, its colour grammar).
void WriteJson(const Value& value, const WriteOptions& options, std::string& out);

// A number as jq prints it: the literal it was read with when it has one,
// else the shortest text that reads back as the same double (1e+16,
// 0.30000000000000004, 1e-05), NaN as "null" and infinities as
// ±1.7976931348623157e+308.
std::string FormatNumber(const Value& value);

// A string as JSON text, in quotes: ", \ and the named escapes, other
// control bytes as \u00XX (0x7F included), lowercase hex; non-ASCII kept
// as its UTF-8 unless |ascii|.
std::string QuoteJsonString(const std::string& utf8, bool ascii);

// The value's compact dump, kept to |bufferSize| - 1 bytes at most: past
// that the first |bufferSize| - 4 bytes are kept (an ill-formed UTF-8
// tail repaired) and "..." follows.
std::string DumpTruncated(const Value& value, size_t bufferSize);

} // namespace Haisos::Jq