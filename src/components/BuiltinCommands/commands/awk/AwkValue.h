#pragma once
#include <cstdint>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Haisos::Awk {

// One awk value: POSIX's three kinds -- number, string, and *strnum*: input
// that looks numeric -- plus the uninitialized value a variable starts with.
class Value {
public:
    enum class Type { Uninitialized, Number, String, StrNum };

    Value();  // Uninitialized: "" and 0 at once
    static Value FromNumber(double number);
    static Value FromString(std::string text);
    // Text that came from input -- fields, $0, var=value operands, -v values,
    // for-in keys, ARGV, getline: StrNum (keeping the text) when it looks
    // numeric (LooksNumeric), else String.
    static Value FromInput(std::string text);

    Type GetType() const;
    // Number, StrNum or Uninitialized: what makes a comparison numeric.
    bool IsNumeric() const;
    // Number/StrNum: the number; String: StringToNumber; Uninitialized: 0.
    double ToNumber() const;
    // Number: AwkNumberToString(number, format); String/StrNum: the text; Uninitialized: "".
    // |format| is CONVFMT for conversions, OFMT for print.
    std::string ToString(const std::string& format) const;
    // Number: != 0; StrNum: its number != 0; String: not empty; Uninitialized: false.
    bool ToBoolean() const;

private:
    Type m_type = Type::Uninitialized;
    double m_number = 0.0;
    std::string m_text;
};

// As gawk --posix converts a string (observed: "3x"+0 is 3, "0x1A"+0 26,
// "info"+0 +inf): leading blanks (space \t \n \v \f \r) skipped, then the
// longest prefix std::strtod accepts (decimal, hex "0x1A", "inf",
// "infinity", "nan", any case) -- none at all is 0.
double StringToNumber(std::string_view text);
// True when that conversion took at least one byte and only blanks follow it.
bool LooksNumeric(std::string_view text, double& number);

// A number as awk shows it:
//  - NaN and infinities: "+nan", "-nan", "+inf", "-inf" (the sign bit decides);
//  - an integral value: its decimal integer, every digit, whatever its size
//    and whatever |format| (as "%.0f" prints it: 1e30 is
//    1000000000000000019884624838656, 2^64 18446744073709551616; "-0" is "0");
//  - otherwise FormatAwkNumber(format, number).
std::string AwkNumberToString(double number, const std::string& format);
// |format| (CONVFMT or OFMT) applied to one number: bytes copied, "%%" a
// '%', each conversion parsed by ParsePrintfSpec and formatted with the
// number -- d i: FormatPrintfSigned of the number truncated toward zero
// (clamped to intmax_t); o u x X: FormatPrintfUnsigned of it (a negative
// value converted as glibc would, through intmax_t); c: FormatPrintfString
// of the byte (unsigned char) of the integer; e E f F g G a A:
// FormatPrintfFloat; any other conversion, or a format ending inside one,
// copied as written ("%q" stays "%q", "abc" "abc", "%5" "%5"). A '*' width
// or precision counts as absent (gawk stops with a fatal error -- a
// documented difference).
std::string FormatAwkNumber(const std::string& format, double number);

// What CompareValues returns when a numeric comparison has a NaN on either
// side: the two are unordered, so < <= == > >= are all false and != true
// (gawk --posix: x = "nan"+0; x == x, x < x and x > x are all 0).
inline constexpr int kAwkUnordered = 2;
// POSIX's comparison: numeric (by ToNumber) when both are IsNumeric(),
// else the two ToString(convfmt) compared byte by byte as unsigned chars.
// Numeric: -1 if a < b, 1 if a > b, 0 if equal, kAwkUnordered with a NaN.
// String: negative, zero or positive (never kAwkUnordered). The interpreter
// maps the result to each relational operator, kAwkUnordered first.
int CompareValues(const Value& a, const Value& b, const std::string& convfmt);

// [A-Za-z_][A-Za-z0-9_]*: a legal awk variable name (for -v and var=value).
bool IsAwkIdentifier(std::string_view name);

// A whole number out of a value's number, as gawk shows it (a field index,
// an NF, an exit code): truncated toward zero. A NaN, or anything outside
// intmax_t's range, is INTMAX_MIN -- gawk's `attempt to access field
// -9223372036854775808' (never a plain cast: out of range it is undefined).
intmax_t AwkIntegerOf(double number);

// An awk array: string keys, insertion-ordered (what `for (k in a)` visits;
// gawk's order is unspecified -- a documented difference).
class AwkArray {
public:
    Value* Find(const std::string& key);          // null when absent
    Value& GetOrCreate(const std::string& key);   // creates an Uninitialized element
    bool Contains(const std::string& key) const;
    void Remove(const std::string& key);
    void Clear();
    size_t Size() const;
    std::vector<std::string> Keys() const;        // a snapshot, insertion order

private:
    // The pairs in insertion order; the index maps a key to its pair. A
    // list keeps every Value reference valid until that element is removed,
    // whatever is inserted or removed around it.
    std::list<std::pair<std::string, Value>> m_pairs;
    std::unordered_map<std::string, std::list<std::pair<std::string, Value>>::iterator> m_index;
};

} // namespace Haisos::Awk