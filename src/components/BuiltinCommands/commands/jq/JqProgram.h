#pragma once

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "commands/jq/JqAst.h"
#include "commands/jq/JqParser.h"
#include "commands/jq/JqRuntime.h"
#include "commands/jq/JqValue.h"

namespace Haisos::Jq {

class JqHost;

// A jq program compiled once: its syntax tree, the Literal values built at
// compile time, and jq 1.7.1's compile-time name checks done (a call of an
// undefined function, an unbound variable, a break of no label, a constant
// object key of the wrong kind -- reported in source order). |Errors| holds
// the parse errors too; a program with errors has no tree and cannot run.
// The prelude's definitions (select, recurse) are in every program's scope.
class Program {
public:
    // Parses |source|, then checks names. |globalNames| are the $variables
    // the command running the program defines ("ENV", "ARGS", its --arg
    // names), without '$': the check accepts them, and Run's |globals|
    // supplies their values.
    static std::shared_ptr<const Program> Compile(
        std::string_view source,
        const std::vector<std::string>& globalNames = {});

    const std::vector<CompileError>& Errors() const;
    bool Ok() const;  // no errors: the tree is there

    // One input through the program: every output handed to |emit|, jq's
    // order. |globals| are the program's $variables (the jq builtin gives it
    // $ENV and friends); an uncaught error leaves as a JqError, a host stop
    // as a JqStopped, and both keep the outputs already emitted.
    void Run(const Value& input, const std::map<std::string, Value>& globals,
             JqHost& host, const Emit& emit) const;

private:
    Program() = default;
    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;

    std::unique_ptr<Node> m_root;
    std::vector<CompileError> m_errors;
    std::unordered_map<const Node*, Value> m_literals;
};

} // namespace Haisos::Jq