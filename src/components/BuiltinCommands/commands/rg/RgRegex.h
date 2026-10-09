#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace Haisos {

// How reading a pattern rg's way went wrong, as ripgrep reports it: where in
// the wrapped pattern (a byte offset), how many carets, and the message after
// "error: ". |auxPosition| names a second byte of the pattern (the first and
// the second '-' of "(?-i-s)", say): the caret line then marks each position
// with a caret of its own instead of one run. |multiline| is not a frame: a
// newline byte spelled as an escape takes rg's multiline message instead.
struct RgRegexError {
    size_t position = 0;      // byte offset in the wrapped pattern
    size_t length = 1;        // carets
    std::string message;      // after "error: "
    bool pcre2Hint = false;   // backreferences and look-around
    bool hasAux = false;      // a second caret at |auxPosition|
    size_t auxPosition = 0;
    bool multiline = false;   // the multiline message, not a frame
};

// |patterns| as given (after -F escaping). Builds the wrapped pattern rg
// shows -- each pattern as "(?:" p ")", joined by "|" -- parses it as Rust
// regex crate syntax in one left-to-right pass and returns the equivalent
// Regex Perl-subset pattern, or nullopt with |error| set. |hasUppercaseLiteral|
// for -S: an ASCII uppercase letter as a literal or a class member, never one
// an escape spells.
std::optional<std::string> TranslateRgPattern(const std::vector<std::string>& patterns,
                                              std::string& wrapped, RgRegexError& error,
                                              bool& hasUppercaseLiteral);

// The frame rg prints for |error|, byte for byte: the pattern and its carets
// indented by four spaces, "rg: " on the first line only.
std::string FormatRgRegexError(const std::string& wrapped, const RgRegexError& error);

} // namespace Haisos