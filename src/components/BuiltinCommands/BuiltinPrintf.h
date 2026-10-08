#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Haisos {

// One conversion specification of a printf format, as glibc reads it:
// %[flags][width][.precision][length]conversion.
struct PrintfSpec {
    std::string flags;                 // in the order given, from "-+ #0'I"
    std::optional<int> width;          // digits, or the '*' argument once the caller has it
    std::optional<int> precision;      // digits ("%.f" is precision 0), or the '*' argument; nullopt: none
    bool widthFromArgument = false;     // '*' was written for the width
    bool precisionFromArgument = false; // '*' was written for the precision
    char conversion = 0;               // the character after the length modifiers, whatever it is
};

// Parses the specification starting at format[pos] == '%'. Flags are any of
// "-+ #0'I" (repeats allowed), then a width (digits, or '*'), then '.' and a
// precision (digits -- none means 0 -- or '*'), then any run of the length
// modifiers h l L j z t (skipped: the caller decides the argument type from
// the conversion), then the conversion character, stored as is (even an
// invalid one: which conversions are valid is the caller's business --
// printf, seq, awk and find differ). "%%" gives conversion '%'.
// True with pos one past the conversion; false when the format ends before a
// conversion character (pos left at the end).
bool ParsePrintfSpec(std::string_view format, size_t& pos, PrintfSpec& spec);

// glibc printf's output of one argument under |spec|, for the conversions
// d i (Signed), o u x X (Unsigned), f F e E g G a A (Float), and c s (String:
// %s takes the whole view, %c the caller's single character as a one-byte
// view). Width and precision count bytes. A negative width (from '*') is the
// '-' flag with its absolute value; a negative precision is no precision.
// The flags ' and I are accepted and have no effect (the C locale groups
// nothing). Never throws; any conversion character not in the function's
// list formats as if it were d / u / g / s respectively.
std::string FormatPrintfSigned(const PrintfSpec& spec, intmax_t value);
std::string FormatPrintfUnsigned(const PrintfSpec& spec, uintmax_t value);
std::string FormatPrintfFloat(const PrintfSpec& spec, long double value);
std::string FormatPrintfString(const PrintfSpec& spec, std::string_view value);

// The result of one backslash escape read by AppendPrintfEscape.
enum class PrintfEscapeResult { Appended, Stop, Error };

// Reads the escape starting at text[pos] == '\\' and appends its bytes to
// |out|; pos ends one past it. GNU printf's print_esc: \" \\ \a \b \e \f \n
// \r \t \v; \c is Stop (nothing more may be output -- the caller stops
// everything, the trailing format and the rest of the arguments included);
// \xH or \xHH (no hex digit: Error, "missing hexadecimal number in escape");
// octal: with |octalZero| false (the format) \NNN, 1 to 3 octal digits; with
// it true (%b) \0NNN (the 0 then up to 3 digits) and also \NNN when the first
// digit is not 0; \uHHHH and \UHHHHHHHH (exactly 4/8 hex digits, else Error
// "missing hexadecimal number in escape"; D800-DFFF is Error "invalid
// universal character name \uD800" -- lowercase hex as GNU prints it, 4 or 8
// digits) appended as UTF-8; any other character: the backslash and that
// character as written; a backslash ending the text: the backslash alone.
// On Error, |error| holds the message (without "printf: ").
PrintfEscapeResult AppendPrintfEscape(std::string_view text, size_t& pos, bool octalZero,
                                      std::string& out, std::string& error);

} // namespace Haisos