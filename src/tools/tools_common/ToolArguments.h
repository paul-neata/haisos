#pragma once
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
#include "interfaces/ILLMService.h"

namespace Haisos::Tools {

// Reading a tool's arguments without ever throwing.
//
// A tool's arguments are whatever an LLM put in the tool call, and small models
// get the types wrong: "append": "true" for a boolean, "path": null for a
// string. nlohmann's accessors throw on such a mismatch -- value() raises
// type_error.302, and get<>() and the implicit conversions do likewise -- and an
// exception out of a tool used to end the agent that called it. Every tool
// therefore reads its arguments through these, which never throw and say what
// was wrong instead:
//   * an optional argument that is absent or null keeps its default;
//   * a required one that is absent or null is "Missing required field: <name>";
//   * one that is present with the wrong type is an error naming it and the
//     type expected -- never quietly the default: "append": "true" read as
//     false would overwrite the very file the model meant to append to.
// Each returns nullopt once the argument is read (or defaulted), and otherwise
// the error ToolResult the tool should return as it is.

namespace ToolArgumentsDetail {

// |args|'s argument |name|, or null when it is absent or null (or |args| is no
// object at all).
inline const nlohmann::json* Find(const nlohmann::json& args, const char* name) {
    if (!args.is_object()) {
        return nullptr;
    }
    const auto it = args.find(name);
    if (it == args.end() || it->is_null()) {
        return nullptr;
    }
    return &*it;
}

// "a string", "an array", "null": a JSON type, as a message names it.
inline std::string TypeInWords(const nlohmann::json& value) {
    const std::string type = value.type_name();
    if (value.is_null()) {
        return type;
    }
    const bool vowel = !type.empty() && std::string("aeiou").find(type[0]) != std::string::npos;
    return (vowel ? "an " : "a ") + type;
}

// What |value| is, for saying why it is not what was expected. An array is
// described by its first item that is not a string, which is what makes an
// array unacceptable where an array of strings is expected.
inline std::string Describe(const nlohmann::json& value) {
    if (value.is_array()) {
        for (const auto& item : value) {
            if (!item.is_string()) {
                return "an array holding " + TypeInWords(item);
            }
        }
    }
    return TypeInWords(value);
}

inline ToolResult WrongType(const char* name, const char* expected, const nlohmann::json& value) {
    return ToolResult{std::string("Invalid field ") + name + ": expected " + expected + ", got " + Describe(value), true};
}

// How each type an argument can have is recognised and read.
template <typename T>
struct Reader;

template <>
struct Reader<std::string> {
    static constexpr const char* kExpected = "a string";
    static bool Read(const nlohmann::json& value, std::string& out) {
        if (!value.is_string()) {
            return false;
        }
        out = value.get<std::string>();
        return true;
    }
};

template <>
struct Reader<bool> {
    static constexpr const char* kExpected = "a boolean (true or false)";
    static bool Read(const nlohmann::json& value, bool& out) {
        if (!value.is_boolean()) {
            return false;
        }
        out = value.get<bool>();
        return true;
    }
};

template <>
struct Reader<uint64_t> {
    static constexpr const char* kExpected = "a non-negative integer";
    static bool Read(const nlohmann::json& value, uint64_t& out) {
        if (value.is_number_unsigned()) {
            out = value.get<uint64_t>();
            return true;
        }
        if (value.is_number_integer()) {
            const int64_t signedValue = value.get<int64_t>();
            if (signedValue < 0) {
                return false;
            }
            out = static_cast<uint64_t>(signedValue);
            return true;
        }
        // Some models write every number with a fraction: 5000.0 is still 5000.
        if (value.is_number_float()) {
            const double floatValue = value.get<double>();
            if (!std::isfinite(floatValue) || floatValue < 0 || std::floor(floatValue) != floatValue ||
                floatValue >= 18446744073709551616.0) {
                return false;
            }
            out = static_cast<uint64_t>(floatValue);
            return true;
        }
        return false;
    }
};

template <>
struct Reader<std::vector<std::string>> {
    static constexpr const char* kExpected = "an array of strings";
    static bool Read(const nlohmann::json& value, std::vector<std::string>& out) {
        if (!value.is_array()) {
            return false;
        }
        std::vector<std::string> strings;
        strings.reserve(value.size());
        for (const auto& item : value) {
            if (!item.is_string()) {
                return false;
            }
            strings.push_back(item.get<std::string>());
        }
        out = std::move(strings);
        return true;
    }
};

} // namespace ToolArgumentsDetail

// Reads the optional argument |name| into |value|, which holds its default and
// keeps it when the argument is absent or null.
template <typename T>
std::optional<ToolResult> ReadOptionalArgument(const nlohmann::json& args, const char* name, T& value) {
    const nlohmann::json* argument = ToolArgumentsDetail::Find(args, name);
    if (!argument || ToolArgumentsDetail::Reader<T>::Read(*argument, value)) {
        return std::nullopt;
    }
    return ToolArgumentsDetail::WrongType(name, ToolArgumentsDetail::Reader<T>::kExpected, *argument);
}

// Reads the optional argument |name| into |value|, left empty when the
// argument is absent or null: for an argument whose absence means something
// that no default value could stand for.
template <typename T>
std::optional<ToolResult> ReadOptionalArgument(const nlohmann::json& args, const char* name, std::optional<T>& value) {
    value.reset();
    const nlohmann::json* argument = ToolArgumentsDetail::Find(args, name);
    if (!argument) {
        return std::nullopt;
    }
    T read{};
    if (!ToolArgumentsDetail::Reader<T>::Read(*argument, read)) {
        return ToolArgumentsDetail::WrongType(name, ToolArgumentsDetail::Reader<T>::kExpected, *argument);
    }
    value = std::move(read);
    return std::nullopt;
}

// Reads the required argument |name| into |value|.
template <typename T>
std::optional<ToolResult> ReadRequiredArgument(const nlohmann::json& args, const char* name, T& value) {
    const nlohmann::json* argument = ToolArgumentsDetail::Find(args, name);
    if (!argument) {
        return ToolResult{std::string("Missing required field: ") + name, true};
    }
    if (ToolArgumentsDetail::Reader<T>::Read(*argument, value)) {
        return std::nullopt;
    }
    return ToolArgumentsDetail::WrongType(name, ToolArgumentsDetail::Reader<T>::kExpected, *argument);
}

} // namespace Haisos::Tools
