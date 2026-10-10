#include "commands/jq/JqIndex.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include "commands/jq/JqJsonWriter.h"
#include "commands/jq/JqRuntime.h"
#include "commands/jq/JqUtf8.h"

namespace Haisos::Jq {
namespace {

// "Cannot index <kind> with <keykind>", or with the string key named while
// it is shorter than 30 bytes (jq prints the key itself only then).
[[noreturn]] void FailIndex(const Value& target, const Value& key) {
    std::string message = "Cannot index ";
    message += KindName(target.GetKind());
    message += " with ";
    if (key.GetKind() == Kind::String && key.AsString().size() < 30) {
        message += "string \"";
        message += key.AsString();
        message += "\"";
    } else {
        message += KindName(key.GetKind());
    }
    throw JqError{Value::String(std::move(message))};
}

// "Cannot iterate over <kind> (<dump 15>)".
[[noreturn]] void FailIterate(const Value& target) {
    std::string message = "Cannot iterate over ";
    message += KindName(target.GetKind());
    message += " (";
    message += DumpTruncated(target, 15);
    message += ")";
    throw JqError{Value::String(std::move(message))};
}

// An array indexed by a number, jq's way: floored, negative from the end,
// null out of range or on NaN.
Value ArrayByNumber(const std::vector<Value>& elements, double index) {
    if (std::isnan(index))
        return Value::Null();
    double i = std::floor(index);
    if (i < 0)
        i += static_cast<double>(elements.size());
    if (i < 0 || i >= static_cast<double>(elements.size()))
        return Value::Null();
    return elements[static_cast<size_t>(i)];
}

// An array indexed by an array: the indices at which the key's elements
// appear in the target, in order (an empty key matches nowhere).
Value ArrayByArray(const std::vector<Value>& elements, const Value& key) {
    const std::vector<Value>& wanted = key.AsArray();
    std::vector<Value> indices;
    if (wanted.empty() || wanted.size() > elements.size())
        return Value::Array(std::move(indices));
    for (size_t i = 0; i + wanted.size() <= elements.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < wanted.size(); ++j) {
            if (!Equal(elements[i + j], wanted[j])) {
                match = false;
                break;
            }
        }
        if (match)
            indices.push_back(Value::Number(static_cast<double>(i)));
    }
    return Value::Array(std::move(indices));
}

// A slice bound: null |from| means 0 (the start) and null |to| the length
// (the end), a negative counts from the end, everything is clamped, the
// from side floored and the to side ceiled, NaN as the null side's value.
size_t SliceBound(const Value& bound, bool from, size_t length) {
    double d;
    if (bound.GetKind() == Kind::Null) {
        d = from ? 0.0 : static_cast<double>(length);
    } else if (bound.GetKind() == Kind::Number) {
        d = bound.AsNumber();
        if (std::isnan(d))
            d = from ? 0.0 : static_cast<double>(length);
        d = from ? std::floor(d) : std::ceil(d);
        if (d < 0)
            d += static_cast<double>(length);
    } else {
        // The caller checks the kind; this is unreachable for a built tree.
        d = from ? 0.0 : static_cast<double>(length);
    }
    if (d < 0)
        d = 0;
    if (d > static_cast<double>(length))
        d = static_cast<double>(length);
    return static_cast<size_t>(d);
}

// The "start"/"end" members of an object key in slice form, or a bound
// that is not a number and not null.
bool SliceForm(const Value& key, const Value*& start, const Value*& end) {
    start = key.Find("start");
    end = key.Find("end");
    if (!start || !end)
        return false;
    const bool startOk = start->GetKind() == Kind::Number ||
                         start->GetKind() == Kind::Null;
    const bool endOk = end->GetKind() == Kind::Number ||
                       end->GetKind() == Kind::Null;
    return startOk && endOk;
}

Value SliceArray(const Value& target, const Value& from, const Value& to) {
    const std::vector<Value>& elements = target.AsArray();
    const size_t length = elements.size();
    const size_t start = SliceBound(from, true, length);
    const size_t stop = SliceBound(to, false, length);
    std::vector<Value> slice;
    for (size_t i = start; i < stop; ++i)
        slice.push_back(elements[i]);
    return Value::Array(std::move(slice));
}

Value SliceString(const Value& target, const Value& from, const Value& to) {
    const std::string& text = target.AsString();
    const size_t length = Utf8Length(text);
    const size_t start = SliceBound(from, true, length);
    const size_t stop = SliceBound(to, false, length);
    const size_t begin = Utf8ByteOffset(text, start);
    const size_t endByte = Utf8ByteOffset(text, stop);
    return Value::String(text.substr(begin, endByte - begin));
}

} // namespace

Value IndexValue(const Value& target, const Value& key) {
    const Kind kind = target.GetKind();
    if (kind == Kind::Null) {
        if (key.GetKind() == Kind::String || key.GetKind() == Kind::Number ||
            key.GetKind() == Kind::Object)
            return Value::Null();
        FailIndex(target, key);
    }
    if (kind == Kind::Object) {
        if (key.GetKind() == Kind::String) {
            const Value* member = target.Find(key.AsString());
            return member ? *member : Value::Null();
        }
        FailIndex(target, key);
    }
    if (kind == Kind::Array) {
        if (key.GetKind() == Kind::Number)
            return ArrayByNumber(target.AsArray(), key.AsNumber());
        if (key.GetKind() == Kind::Array)
            return ArrayByArray(target.AsArray(), key);
        if (key.GetKind() == Kind::Object) {
            const Value* start = nullptr;
            const Value* end = nullptr;
            if (SliceForm(key, start, end))
                return SliceArray(target, *start, *end);
            throw JqError{
                Value::String("Array/string slice indices must be integers")};
        }
        FailIndex(target, key);
    }
    FailIndex(target, key);
}

Value SliceValue(const Value& target, const Value& from, const Value& to) {
    const Kind kind = target.GetKind();
    if (kind == Kind::Null)
        return Value::Null();
    if (kind == Kind::Array || kind == Kind::String) {
        if ((from.GetKind() != Kind::Number && from.GetKind() != Kind::Null) ||
            (to.GetKind() != Kind::Number && to.GetKind() != Kind::Null))
            throw JqError{
                Value::String("Array/string slice indices must be integers")};
        return kind == Kind::Array ? SliceArray(target, from, to)
                                   : SliceString(target, from, to);
    }
    // A slice reaches the target as an index by an object in slice form,
    // so the message names the key: an object target included.
    FailIndex(target, Value::Object({}));
}

void IterateValue(const Value& target,
                  const std::function<void(const Value&)>& each) {
    if (target.GetKind() == Kind::Array) {
        for (const Value& element : target.AsArray())
            each(element);
        return;
    }
    if (target.GetKind() == Kind::Object) {
        for (const auto& entry : target.AsObject())
            each(entry.second);
        return;
    }
    FailIterate(target);
}

} // namespace Haisos::Jq