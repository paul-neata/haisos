#include "commands/jq/JqValue.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

namespace Haisos::Jq {

// From this many members on, an object's payload carries a key index.
constexpr size_t kObjectIndexThreshold = 16;

// An object's members in insertion order, with a key-to-position index
// from kObjectIndexThreshold members up.
struct Value::ObjectPayload {
    std::vector<ObjectEntry> entries;
    std::unordered_map<std::string, size_t> index;
};

const char* KindName(Kind kind) {
    switch (kind) {
        case Kind::Null: return "null";
        case Kind::False:
        case Kind::True: return "boolean";
        case Kind::Number: return "number";
        case Kind::String: return "string";
        case Kind::Array: return "array";
        case Kind::Object: return "object";
    }
    return "";
}

Value::Value() = default;

Value Value::Null() { return Value(); }

Value Value::Boolean(bool b) {
    Value v;
    v.m_kind = b ? Kind::True : Kind::False;
    return v;
}

Value Value::Number(double d) {
    Value v;
    v.m_kind = Kind::Number;
    v.m_number = d;
    return v;
}

Value Value::NumberLiteral(double d, std::string canonicalLiteral) {
    Value v;
    v.m_kind = Kind::Number;
    v.m_number = d;
    if (!canonicalLiteral.empty())
        v.m_literal = std::make_shared<const std::string>(std::move(canonicalLiteral));
    return v;
}

Value Value::String(std::string utf8) {
    Value v;
    v.m_kind = Kind::String;
    v.m_string = std::make_shared<const std::string>(std::move(utf8));
    return v;
}

Value Value::Array(std::vector<Value> elements) {
    Value v;
    v.m_kind = Kind::Array;
    v.m_array = std::make_shared<const std::vector<Value>>(std::move(elements));
    return v;
}

Value Value::Object(std::vector<ObjectEntry> entries) {
    Value v;
    v.m_kind = Kind::Object;
    auto payload = std::make_unique<ObjectPayload>();
    payload->entries = std::move(entries);
    if (payload->entries.size() >= kObjectIndexThreshold)
        for (size_t i = 0; i < payload->entries.size(); ++i)
            payload->index.emplace(payload->entries[i].first, i);
    v.m_object = std::move(payload);
    return v;
}

Kind Value::GetKind() const { return m_kind; }

bool Value::IsTruthy() const { return m_kind != Kind::Null && m_kind != Kind::False; }

double Value::AsNumber() const { return m_kind == Kind::Number ? m_number : 0.0; }

const std::string* Value::Literal() const {
    return m_kind == Kind::Number ? m_literal.get() : nullptr;
}

const std::string& Value::AsString() const {
    static const std::string empty;
    return m_string ? *m_string : empty;
}

const std::vector<Value>& Value::AsArray() const {
    static const std::vector<Value> empty;
    return m_array ? *m_array : empty;
}

const std::vector<ObjectEntry>& Value::AsObject() const {
    static const std::vector<ObjectEntry> empty;
    return m_object ? m_object->entries : empty;
}

const Value* Value::Find(const std::string& key) const {
    if (m_kind != Kind::Object || !m_object)
        return nullptr;
    const ObjectPayload& payload = *m_object;
    if (!payload.index.empty()) {
        const auto it = payload.index.find(key);
        return it == payload.index.end() ? nullptr
                                         : &payload.entries[it->second].second;
    }
    for (const ObjectEntry& entry : payload.entries)
        if (entry.first == key)
            return &entry.second;
    return nullptr;
}

// The position of |key| in |payload|'s members, or SIZE_MAX when absent.
size_t Value::MemberPosition(const ObjectPayload& payload,
                             const std::string& key) {
    if (!payload.index.empty()) {
        const auto it = payload.index.find(key);
        return it == payload.index.end() ? std::string::npos : it->second;
    }
    for (size_t i = 0; i < payload.entries.size(); ++i)
        if (payload.entries[i].first == key)
            return i;
    return std::string::npos;
}

Value Value::WithMember(const std::string& key, Value v) const {
    if (m_kind == Kind::Object && m_object) {
        const size_t at = MemberPosition(*m_object, key);
        if (at != std::string::npos) {  // an existing member keeps its place
            std::vector<ObjectEntry> entries = m_object->entries;
            entries[at].second = std::move(v);
            return Object(std::move(entries));
        }
        std::vector<ObjectEntry> entries = m_object->entries;
        entries.emplace_back(key, std::move(v));
        return Object(std::move(entries));
    }
    std::vector<ObjectEntry> entries;
    entries.emplace_back(key, std::move(v));
    return Object(std::move(entries));
}

Value Value::WithoutMember(const std::string& key) const {
    std::vector<ObjectEntry> entries;
    if (m_kind == Kind::Object && m_object)
        entries = m_object->entries;
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                [&key](const ObjectEntry& entry) { return entry.first == key; }),
                 entries.end());
    return Object(std::move(entries));
}

Value Value::WithElement(size_t index, Value v) const {
    std::vector<Value> elements;
    if (m_kind == Kind::Array && m_array)
        elements = *m_array;
    if (index >= elements.size())
        return *this;
    elements[index] = std::move(v);
    return Array(std::move(elements));
}

namespace {
// A literal taken apart for comparison: sign, its digits with leading and
// trailing zeros stripped, and the exponent of its first digit.
struct LiteralParts {
    bool negative = false;
    std::string digits;  // stripped; empty means zero
    long long adjusted = 0;  // the power of ten of the first digit
};

bool IsDigitChar(char c) { return c >= '0' && c <= '9'; }

bool ParseLiteralParts(const std::string& s, LiteralParts& out) {
    size_t i = 0;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
        out.negative = s[i] == '-';
        ++i;
    }
    std::string digits;
    long long exponent = 0;
    while (i < s.size() && IsDigitChar(s[i]))
        digits += s[i++];
    if (i < s.size() && s[i] == '.') {
        ++i;
        const size_t fracStart = i;
        while (i < s.size() && IsDigitChar(s[i]))
            digits += s[i++];
        exponent -= static_cast<long long>(i - fracStart);
    }
    if (digits.empty())
        return false;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        bool expNegative = false;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
            expNegative = s[i] == '-';
            ++i;
        }
        const size_t expStart = i;
        long long expValue = 0;
        while (i < s.size() && IsDigitChar(s[i])) {
            if (expValue < 1000000000000000000LL)
                expValue = expValue * 10 + (s[i] - '0');
            ++i;
        }
        if (i == expStart)
            return false;
        exponent += expNegative ? -expValue : expValue;
    }
    if (i != s.size())
        return false;
    const size_t first = digits.find_first_not_of('0');
    if (first == std::string::npos) {
        out.digits.clear();  // zero
        return true;
    }
    const size_t last = digits.find_last_not_of('0');
    out.digits = digits.substr(first, last - first + 1);
    // The power of ten of the first digit: the whole digit string counts,
    // trailing zeros included (they were stripped from out.digits alone).
    out.adjusted = exponent + static_cast<long long>(digits.size()) - 1 -
                   static_cast<long long>(first);
    return true;
}

// The magnitudes of two nonzero literals, sign aside.
int CompareLiteralMagnitude(const LiteralParts& a, const LiteralParts& b) {
    if (a.adjusted != b.adjusted)
        return a.adjusted < b.adjusted ? -1 : 1;
    const int c = a.digits.compare(b.digits);
    return c < 0 ? -1 : c > 0 ? 1 : 0;
}

int CompareNumbers(const Value& a, const Value& b) {
    // A NaN sorts below every other number, itself included (jq's sort).
    if (std::isnan(a.AsNumber()))
        return -1;
    if (std::isnan(b.AsNumber()))
        return 1;
    if (a.Literal() && b.Literal())
        return CompareDecimalLiterals(*a.Literal(), *b.Literal());
    if (a.AsNumber() < b.AsNumber())
        return -1;
    if (a.AsNumber() > b.AsNumber())
        return 1;
    return 0;
}

int CompareObjects(const Value& a, const Value& b) {
    // The key lists, sorted, decide first; then the values, each pair taken
    // under the same key.
    std::vector<const std::string*> keysA, keysB;
    for (const ObjectEntry& entry : a.AsObject())
        keysA.push_back(&entry.first);
    for (const ObjectEntry& entry : b.AsObject())
        keysB.push_back(&entry.first);
    const auto byText = [](const std::string* x, const std::string* y) { return *x < *y; };
    std::sort(keysA.begin(), keysA.end(), byText);
    std::sort(keysB.begin(), keysB.end(), byText);
    const size_t common = std::min(keysA.size(), keysB.size());
    for (size_t i = 0; i < common; ++i) {
        if (*keysA[i] != *keysB[i])
            return *keysA[i] < *keysB[i] ? -1 : 1;
    }
    if (keysA.size() != keysB.size())
        return keysA.size() < keysB.size() ? -1 : 1;
    for (size_t i = 0; i < keysA.size(); ++i) {
        const int c = Compare(*a.Find(*keysA[i]), *b.Find(*keysB[i]));
        if (c != 0)
            return c;
    }
    return 0;
}

} // namespace

int Compare(const Value& a, const Value& b) {
    if (a.GetKind() != b.GetKind())
        return a.GetKind() < b.GetKind() ? -1 : 1;
    switch (a.GetKind()) {
        case Kind::Null:
        case Kind::False:
        case Kind::True:
            return 0;
        case Kind::Number:
            return CompareNumbers(a, b);
        case Kind::String: {
            const int c = a.AsString().compare(b.AsString());
            return c < 0 ? -1 : c > 0 ? 1 : 0;
        }
        case Kind::Array: {
            const std::vector<Value>& x = a.AsArray();
            const std::vector<Value>& y = b.AsArray();
            const size_t common = std::min(x.size(), y.size());
            for (size_t i = 0; i < common; ++i) {
                const int c = Compare(x[i], y[i]);
                if (c != 0)
                    return c;
            }
            if (x.size() != y.size())
                return x.size() < y.size() ? -1 : 1;
            return 0;
        }
        case Kind::Object:
            return CompareObjects(a, b);
    }
    return 0;
}

bool Equal(const Value& a, const Value& b) { return Compare(a, b) == 0; }

int CompareDecimalLiterals(const std::string& a, const std::string& b) {
    LiteralParts pa, pb;
    if (!ParseLiteralParts(a, pa) || !ParseLiteralParts(b, pb))
        return 0;  // not literals: not comparable
    if (pa.digits.empty() && pb.digits.empty())
        return 0;  // both zero: -0 == 0
    if (pa.digits.empty())
        return pb.negative ? 1 : -1;
    if (pb.digits.empty())
        return pa.negative ? -1 : 1;
    if (pa.negative != pb.negative)
        return pa.negative ? -1 : 1;
    const int c = CompareLiteralMagnitude(pa, pb);
    return pa.negative ? -c : c;
}

bool CanonicalNumberLiteral(std::string_view text, std::string& canonical,
                            double& value) {
    canonical.clear();
    if (text.empty())
        return false;
    size_t i = 0;
    bool negative = false;
    if (text[i] == '+' || text[i] == '-') {
        negative = text[i] == '-';
        ++i;
    }
    std::string_view rest = text.substr(i);
    size_t p = 0;
    const size_t intStart = p;
    while (p < rest.size() && IsDigitChar(rest[p]))
        ++p;
    const std::string_view intDigits = rest.substr(intStart, p - intStart);
    std::string_view fracDigits;
    if (p < rest.size() && rest[p] == '.') {
        ++p;
        const size_t fracStart = p;
        while (p < rest.size() && IsDigitChar(rest[p]))
            ++p;
        fracDigits = rest.substr(fracStart, p - fracStart);
    }
    if (intDigits.empty() && fracDigits.empty())
        return false;
    long long exponent = 0;
    if (p < rest.size() && (rest[p] == 'e' || rest[p] == 'E')) {
        ++p;
        bool expNegative = false;
        if (p < rest.size() && (rest[p] == '+' || rest[p] == '-')) {
            expNegative = rest[p] == '-';
            ++p;
        }
        const size_t expStart = p;
        long long expValue = 0;
        while (p < rest.size() && IsDigitChar(rest[p])) {
            if (expValue < 1000000000000000000LL)
                expValue = expValue * 10 + (rest[p] - '0');
            ++p;
        }
        if (p == expStart)
            return false;  // an 'e' with no digits is not a number
        exponent += expNegative ? -expValue : expValue;
    }
    if (p != rest.size())
        return false;  // trailing characters are not part of a number

    std::string digits(intDigits);
    digits.append(fracDigits);
    exponent -= static_cast<long long>(fracDigits.size());
    value = std::strtod(std::string(text).c_str(), nullptr);

    // Leading zeros drop away, one digit kept ("01" -> "1", "00.5" -> "0.5");
    // "0" is the digit of a zero.
    const size_t first = digits.find_first_not_of('0');
    if (first == std::string::npos) {
        digits = "0";
    } else {
        digits = digits.substr(first);
    }
    const long long n = static_cast<long long>(digits.size());
    const long long adjusted = exponent + n - 1;
    if (adjusted > 999999999) {
        // Too large a literal to keep: the value alone decides.
        return true;
    }

    canonical = negative ? "-" : "";
    const bool plain = exponent <= 0 && adjusted >= -6;
    if (!plain) {
        canonical += digits[0];
        if (n > 1) {
            canonical += '.';
            canonical.append(digits, 1, std::string::npos);
        }
        canonical += 'E';
        canonical += adjusted >= 0 ? '+' : '-';
        canonical += std::to_string(adjusted >= 0 ? adjusted : -adjusted);
        return true;
    }
    const long long insert = n + exponent;  // digits before the dot
    if (insert <= 0) {
        canonical += "0.";
        canonical.append(static_cast<size_t>(-insert), '0');
        canonical += digits;
    } else if (insert >= n) {
        canonical += digits;
        canonical.append(static_cast<size_t>(insert - n), '0');
    } else {
        canonical.append(digits, 0, static_cast<size_t>(insert));
        canonical += '.';
        canonical.append(digits, static_cast<size_t>(insert), std::string::npos);
    }
    return true;
}

} // namespace Haisos::Jq