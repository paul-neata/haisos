#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Haisos::Jq {

// jq's kinds, in jq's sort order.
enum class Kind { Null, False, True, Number, String, Array, Object };

// "null", "boolean" (False and True alike), "number", "string", "array",
// "object" -- the names jq's error messages use.
const char* KindName(Kind kind);

class Value;
using ObjectEntry = std::pair<std::string, Value>;

// A value of the jq language: immutable, its payload shared by every copy.
// A number read from JSON (or written in a program) keeps the text it was
// written in, in a canonical form (CanonicalNumberLiteral): `1.0` prints
// `1.0`, `1e2` prints `1E+2`. Objects keep their members in insertion
// order; a `+` replaces an existing member in place (jq's `{"a":1} +
// {"a":2}` keeps `a` first), and removing then adding a member appends it.
class Value {
public:
    Value();  // null

    static Value Null();
    static Value Boolean(bool b);
    static Value Number(double d);  // a computed number: no literal
    // A number with the text it was written in, as CanonicalNumberLiteral
    // gives it; an empty |canonicalLiteral| means none (a computed number).
    static Value NumberLiteral(double d, std::string canonicalLiteral);
    static Value String(std::string utf8);
    static Value Array(std::vector<Value> elements);
    static Value Object(std::vector<ObjectEntry> entries);  // keys unique, in order

    Kind GetKind() const;
    bool IsTruthy() const;  // not null, not false
    double AsNumber() const;
    // The canonical literal of a number, or null when it has none.
    const std::string* Literal() const;
    const std::string& AsString() const;
    const std::vector<Value>& AsArray() const;
    const std::vector<ObjectEntry>& AsObject() const;
    const Value* Find(const std::string& key) const;  // object member or null

    // Copies with one change (the original is untouched):
    Value WithMember(const std::string& key, Value v) const;  // replace in place, or append
    Value WithoutMember(const std::string& key) const;        // removed; order of the rest kept
    Value WithElement(size_t index, Value v) const;          // index < size

private:
    Kind m_kind = Kind::Null;
    double m_number = 0.0;
    std::shared_ptr<const std::string> m_literal;             // numbers
    std::shared_ptr<const std::string> m_string;              // strings
    std::shared_ptr<const std::vector<Value>> m_array;        // arrays
    std::shared_ptr<const std::vector<ObjectEntry>> m_object; // objects
};

// jq's total order, as `sort`, `<` and `==` show it: by Kind first; numbers
// -- when both have a literal, compared exactly as decimals
// (CompareDecimalLiterals), otherwise as doubles, a NaN smaller than every
// number and than itself; strings byte by byte, a prefix first; arrays
// element by element, then the shorter first; objects by their sorted key
// lists first (compared as arrays of strings), then by their values taken
// in sorted key order.
int Compare(const Value& a, const Value& b);
bool Equal(const Value& a, const Value& b);  // Compare == 0

// Exact comparison of two canonical literals (sign, digits, exponent):
// "1.0" == "1", "1.10" == "1.1", "100000000000000000001" >
// "100000000000000000000", "-0" == "0".
int CompareDecimalLiterals(const std::string& a, const std::string& b);

// The canonical form of a number written in JSON or a jq program ("1.0",
// "1E+2", ".5" -> "0.5", "01" -> "1"), and its double; the plan's number
// layout. An adjusted exponent above 999999999 keeps no literal (an empty
// |canonical|) and the value is the double. Returns false if |text| is not
// a number.
bool CanonicalNumberLiteral(std::string_view text, std::string& canonical,
                            double& value);

} // namespace Haisos::Jq