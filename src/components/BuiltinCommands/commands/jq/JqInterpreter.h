#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "commands/jq/JqAst.h"
#include "commands/jq/JqRuntime.h"

namespace Haisos::Jq {

class NativeRegistry;

// The scope a program runs in: one binding per node and a parent, looked
// up by walking the chain (innermost first). Immutable -- binding extends
// it with a new node, and the old scope is never changed.
struct Env;
using EnvPtr = std::shared_ptr<const Env>;

// A filter argument, called where its function was called: its body runs
// in the env it was defined in, on the caller's input.
struct Closure {
    const Node* body = nullptr;
    EnvPtr env;
};

// The evaluation recursion is bounded (a documented difference: jq's is
// as deep as memory allows): one level per active Eval call, whatever the
// construct, so recursion through user functions, tall trees and closures
// is bounded by the one guard. Past it, a JqError
// "Maximum evaluation depth (1024) exceeded".
constexpr int kJqMaxEvalDepth = 1024;

// The bindings, each one Env node extending |parent| (defined in
// JqInterpreter.cpp): a variable; a function (its definition and, weakly,
// the env it was defined in -- weakly because a recursive definition's env
// holds the binding that points back at it, an ownership cycle otherwise);
// a filter parameter (a Closure over the caller's env); a label (the
// instance id of one run through its body).
EnvPtr MakeVariableEnv(const EnvPtr& parent, std::string name, Value value);
EnvPtr MakeFunctionEnv(const EnvPtr& parent, const FunctionDefinition& definition);
EnvPtr MakeLabelEnv(const EnvPtr& parent, std::string name, uint64_t label);

// The value a Literal's text stands for: "true"/"false"/"null" or a number
// as written (kept as its literal). The compile-time cache holds it; a miss
// builds it again.
Value LiteralValueOf(const std::string& text);

class Interpreter {
public:
    // |literals| is the Literal values built once at compile time (may be
    // null: a Literal is then built from its text when it is evaluated).
    Interpreter(const NativeRegistry& natives, JqHost& host,
                const std::unordered_map<const Node*, Value>* literals = nullptr);

    // Every output of |node| on |input|, in jq 1.7.1's order, one emit per
    // output. Errors are JqError exceptions, break JqBreak, a host stop
    // JqStopped; none is caught here.
    void Eval(const Node& node, const Value& input, const EnvPtr& env,
              const Emit& emit);
    // A filter parameter's body, in its own env, on the current input.
    void EvalClosure(const Closure& closure, const Value& input, const Emit& emit);

    JqHost& Host();
    void CheckStop();  // throws JqStopped when the host says so

private:
    // One helper per node kind: Eval only dispatches, so the big cases do
    // not swell every frame (the depth guard bounds the stack).
    void EvalRecurseDefault(const Value& input, const Emit& emit);
    void EvalIndex(const Node& node, const Value& input, const EnvPtr& env,
                   const Emit& emit);
    void EvalSlice(const Node& node, const Value& input, const EnvPtr& env,
                   const Emit& emit);
    void EvalIterate(const Node& node, const Value& input, const EnvPtr& env,
                     const Emit& emit);
    void EvalTry(const Node& node, const Value& input, const EnvPtr& env,
                 const Emit& emit);
    void EvalLiteral(const Node& node, const Emit& emit);
    void EvalString(const Node& node, const Value& input, const EnvPtr& env,
                    const Emit& emit);
    void EvalFormat(const Node& node, const Value& input, const Emit& emit);
    void EvalArray(const Node& node, const Value& input, const EnvPtr& env,
                   const Emit& emit);
    void EvalObject(const Node& node, const Value& input, const EnvPtr& env,
                    const Emit& emit);
    void EvalNegate(const Node& node, const Value& input, const EnvPtr& env,
                    const Emit& emit);
    void EvalAlternative(const Node& node, const Value& input, const EnvPtr& env,
                          const Emit& emit);
    void EvalAndOr(const Node& node, const Value& input, const EnvPtr& env,
                   const Emit& emit);
    void EvalBinary(const Node& node, const Value& input, const EnvPtr& env,
                     const Emit& emit);
    void EvalIf(const Node& node, const Value& input, const EnvPtr& env,
                const Emit& emit);
    void EvalReduce(const Node& node, const Value& input, const EnvPtr& env,
                    const Emit& emit);
    void EvalForeach(const Node& node, const Value& input, const EnvPtr& env,
                     const Emit& emit);
    void EvalBind(const Node& node, const Value& input, const EnvPtr& env,
                  const Emit& emit);
    void EvalDefs(const Node& node, const Value& input, const EnvPtr& env,
                  const Emit& emit);
    void EvalCall(const Node& node, const Value& input, const EnvPtr& env,
                  const Emit& emit);
    void EvalLabel(const Node& node, const Value& input, const EnvPtr& env,
                   const Emit& emit);

    const NativeRegistry& m_natives;
    JqHost& m_host;
    const std::unordered_map<const Node*, Value>* m_literals;
    int m_depth = 0;
    uint64_t m_nextTryId = 1;  // each try tells its own errors from downstream ones
    uint64_t m_nextLabel = 1;  // label instance ids
};

} // namespace Haisos::Jq