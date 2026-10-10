#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "commands/jq/JqValue.h"

namespace Haisos::Jq {

// A jq error: error/0,1's value, or a runtime error's message as a string.
struct JqError {
    Value value;
};

// Thrown when the host asks to stop (the process running the program was
// stopped); never caught by try/catch.
struct JqStopped {};

// break $label: the label instance it unwinds to (Interpreter's counter).
struct JqBreak {
    uint64_t label = 0;
};

// What a running program reaches outside itself: its inputs, the standard
// error, the input's place. The jq builtin implements it over its
// BuiltinContext -- the ICurrentProcess rule: a program reaches nothing of
// the process but through here. A test stub implements it too.
class JqHost {
public:
    virtual ~JqHost() = default;
    // input/inputs: the next input value, nullopt at the end.
    virtual std::optional<Value> NextInput() = 0;
    virtual void WriteStderr(const std::string& bytes) = 0;
    virtual Value InputFilename() const = 0;  // a string, or null
    virtual int InputLineNumber() const = 0;
    virtual bool StopRequested() const = 0;
};

// One output of a program, handed to the emitter as it is produced.
using Emit = std::function<void(const Value&)>;

} // namespace Haisos::Jq