#include "commands/jq/JqArithmetic.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "commands/jq/JqJsonWriter.h"
#include "commands/jq/JqRuntime.h"

namespace Haisos::Jq {
namespace {

// jq's message of an arithmetic failure, byte for byte as jq 1.7.1 prints
// it: the two kinds and their dumps (kept to 15 bytes) and what cannot be
// done with them.
[[noreturn]] void Fail(const char* what, const Value& a, const Value& b) {
    std::string message = KindName(a.GetKind());
    message += " (";
    message += DumpTruncated(a, 15);
    message += ") and ";
    message += KindName(b.GetKind());
    message += " (";
    message += DumpTruncated(b, 15);
    message += ") cannot be ";
    message += what;
    throw JqError{Value::String(std::move(message))};
}

// The same message with the zero-divisor suffix jq gives a division by
// zero -- also when the divisor reaches zero only by truncation (5 % 0.5).
[[noreturn]] void FailZeroDivisor(const char* what, const Value& a,
                                  const Value& b) {
    std::string message = KindName(a.GetKind());
    message += " (";
    message += DumpTruncated(a, 15);
    message += ") and ";
    message += KindName(b.GetKind());
    message += " (";
    message += DumpTruncated(b, 15);
    message += ") cannot be ";
    message += what;
    message += " because the divisor is zero";
    throw JqError{Value::String(std::move(message))};
}

bool Both(const Value& a, const Value& b, Kind kind) {
    return a.GetKind() == kind && b.GetKind() == kind;
}

// The two objects merged, the right one's members replacing in place and
// new ones appended, as jq's + merges objects.
Value MergeObjects(const Value& a, const Value& b) {
    std::vector<ObjectEntry> merged = a.AsObject();
    for (const auto& entry : b.AsObject()) {
        bool found = false;
        for (auto& out : merged) {
            if (out.first == entry.first) {
                out.second = entry.second;
                found = true;
                break;
            }
        }
        if (!found)
            merged.push_back(entry);
    }
    return Value::Object(std::move(merged));
}

// The two objects merged recursively, as jq's * merges them: a member both
// hold as objects is merged the same way, any other right value wins.
Value MergeDeep(const Value& a, const Value& b) {
    if (!Both(a, b, Kind::Object))
        return b;
    std::vector<ObjectEntry> merged = a.AsObject();
    for (const auto& entry : b.AsObject()) {
        bool found = false;
        for (auto& out : merged) {
            if (out.first == entry.first) {
                out.second = MergeDeep(out.second, entry.second);
                found = true;
                break;
            }
        }
        if (!found)
            merged.push_back(entry);
    }
    return Value::Object(std::move(merged));
}

// jq's % on numbers (observed on jq 1.7.1): a NaN operand gives NaN; else
// both magnitudes are truncated towards zero, saturating at 2^63-1 (an
// infinity too); a truncated divisor of zero fails; the result is the
// remainder of the magnitudes carrying the dividend's sign, -0 included.
Value ModuloNumber(const Value& a, const Value& b) {
    const double x = a.AsNumber();
    const double y = b.AsNumber();
    if (std::isnan(x) || std::isnan(y))
        return Value::Number(std::numeric_limits<double>::quiet_NaN());
    const uint64_t kMagnitudeLimit = 0x7FFFFFFFFFFFFFFFull;
    auto Truncate = [kMagnitudeLimit](double d) -> uint64_t {
        const double magnitude = std::fabs(std::trunc(d));
        if (!(magnitude < static_cast<double>(kMagnitudeLimit)))
            return kMagnitudeLimit;  // saturates: an infinity and beyond 2^63
        return static_cast<uint64_t>(magnitude);
    };
    const uint64_t dividend = Truncate(x);
    const uint64_t divisor = Truncate(y);
    if (divisor == 0)
        FailZeroDivisor("divided (remainder)", a, b);
    const double remainder =
        std::copysign(static_cast<double>(dividend % divisor), x);
    return Value::Number(remainder);
}

// The left split on every occurrence of |separator| ("" each byte), as jq's
// / splits two strings.
Value SplitString(const Value& a, const Value& b) {
    const std::string& text = a.AsString();
    const std::string& separator = b.AsString();
    std::vector<Value> pieces;
    if (separator.empty()) {
        for (char c : text)
            pieces.push_back(Value::String(std::string(1, c)));
        return Value::Array(std::move(pieces));
    }
    size_t start = 0;
    for (;;) {
        const size_t found = text.find(separator, start);
        if (found == std::string::npos)
            break;
        pieces.push_back(Value::String(text.substr(start, found - start)));
        start = found + separator.size();
    }
    pieces.push_back(Value::String(text.substr(start)));
    return Value::Array(std::move(pieces));
}

} // namespace

Value Add(const Value& a, const Value& b) {
    // null + x and x + null are x itself (its literal survives).
    if (a.GetKind() == Kind::Null)
        return b;
    if (b.GetKind() == Kind::Null)
        return a;
    if (Both(a, b, Kind::Number))
        return Value::Number(a.AsNumber() + b.AsNumber());
    if (Both(a, b, Kind::String))
        return Value::String(a.AsString() + b.AsString());
    if (Both(a, b, Kind::Array)) {
        std::vector<Value> joined = a.AsArray();
        const std::vector<Value>& right = b.AsArray();
        joined.insert(joined.end(), right.begin(), right.end());
        return Value::Array(std::move(joined));
    }
    if (Both(a, b, Kind::Object))
        return MergeObjects(a, b);
    Fail("added", a, b);
}

Value Subtract(const Value& a, const Value& b) {
    if (Both(a, b, Kind::Number))
        return Value::Number(a.AsNumber() - b.AsNumber());
    if (Both(a, b, Kind::Array)) {
        // The left without every element equal to one of the right's.
        std::vector<Value> kept;
        for (const Value& element : a.AsArray()) {
            bool remove = false;
            for (const Value& other : b.AsArray()) {
                if (Equal(element, other)) {
                    remove = true;
                    break;
                }
            }
            if (!remove)
                kept.push_back(element);
        }
        return Value::Array(std::move(kept));
    }
    Fail("subtracted", a, b);
}

Value Multiply(const Value& a, const Value& b) {
    if (Both(a, b, Kind::Number))
        return Value::Number(a.AsNumber() * b.AsNumber());
    // A string and a number, either order: the string repeated floor(n)
    // times, null for a negative or NaN count.
    const bool aString = a.GetKind() == Kind::String;
    const bool bString = b.GetKind() == Kind::String;
    if (aString != bString &&
        (a.GetKind() == Kind::Number || b.GetKind() == Kind::Number)) {
        const Value& text = aString ? a : b;
        const double count = aString ? b.AsNumber() : a.AsNumber();
        if (!(count >= 0))  // negative or NaN
            return Value::Null();
        const double times = std::floor(count);
        std::string out;
        for (double i = 0; i < times; ++i)
            out += text.AsString();
        return Value::String(std::move(out));
    }
    if (Both(a, b, Kind::Object))
        return MergeDeep(a, b);
    Fail("multiplied", a, b);
}

Value Divide(const Value& a, const Value& b) {
    if (Both(a, b, Kind::Number)) {
        if (b.AsNumber() == 0.0)
            FailZeroDivisor("divided", a, b);
        return Value::Number(a.AsNumber() / b.AsNumber());
    }
    if (Both(a, b, Kind::String))
        return SplitString(a, b);
    Fail("divided", a, b);
}

Value Modulo(const Value& a, const Value& b) {
    if (Both(a, b, Kind::Number))
        return ModuloNumber(a, b);
    Fail("divided (remainder)", a, b);
}

} // namespace Haisos::Jq