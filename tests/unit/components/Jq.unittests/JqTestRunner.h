#pragma once

#include <optional>
#include <string>

#include "commands/jq/JqJsonReader.h"
#include "commands/jq/JqJsonWriter.h"
#include "commands/jq/JqParser.h"
#include "commands/jq/JqProgram.h"
#include "commands/jq/JqRuntime.h"

namespace Haisos::Jq {

// The stub host of the jq tests: no inputs, no file, never stopping.
class TestJqHost : public JqHost {
public:
    std::optional<Value> NextInput() override { return std::nullopt; }
    void WriteStderr(const std::string&) override {}
    Value InputFilename() const override { return Value(); }
    int InputLineNumber() const override { return 0; }
    bool StopRequested() const override { return false; }
};

// A program compiled and run on one input, as the jq builtin prints it:
// every output compact, single-space separated -- or, on an uncaught
// error, the outputs so far and then `error: <message>` (the error's
// string, or `(not a string): <compact JSON>`). Compile errors come back
// as jq prints them (every location block, then the count line).
inline std::string RunJq(const std::string& program,
                         const std::string& inputJson = "null") {
    auto compiled = Program::Compile(program);
    if (!compiled->Ok()) {
        std::string out;
        for (const CompileError& error : compiled->Errors())
            out += FormatCompileError(program, error);
        out += FormatCompileErrorCount(compiled->Errors().size());
        return out;
    }
    Value input;
    std::string readError;
    if (!ParseSingleJson(inputJson, input, readError)) {
        return "input error: " + readError;
    }
    TestJqHost host;
    WriteOptions options;
    options.indent = 0;
    std::string outputs;
    try {
        compiled->Run(input, {}, host, [&](const Value& value) {
            if (!outputs.empty())
                outputs += ' ';
            WriteJson(value, options, outputs);
        });
    } catch (const JqError& error) {
        if (!outputs.empty())
            outputs += ' ';
        outputs += "error: ";
        if (error.value.GetKind() == Kind::String) {
            outputs += error.value.AsString();
        } else {
            outputs += "(not a string): ";
            WriteJson(error.value, options, outputs);
        }
    }
    return outputs;
}

} // namespace Haisos::Jq