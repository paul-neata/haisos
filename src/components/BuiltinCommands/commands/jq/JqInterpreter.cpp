#include "commands/jq/JqInterpreter.h"

#include <cassert>
#include <string>
#include <vector>

#include "commands/jq/JqArithmetic.h"
#include "commands/jq/JqFormat.h"
#include "commands/jq/JqIndex.h"
#include "commands/jq/JqJsonWriter.h"
#include "commands/jq/JqNatives.h"

namespace Haisos::Jq {

struct Env {
    enum class Kind { Variable, Function, FilterParam, Label };
    Kind kind = Kind::Variable;
    std::string name;      // every kind, without '$' or '@'
    int arity = 0;         // Function (a filter parameter and a $value
                           // parameter's filter are arity 0)
    Value value;           // Variable
    const FunctionDefinition* definition = nullptr;  // Function
    std::weak_ptr<const Env> definitionEnv;  // Function: where its body runs
                                             // (weakly: a recursive
                                             // definition's own env holds the
                                             // binding, a cycle otherwise)
    Closure closure;       // FilterParam
    uint64_t label = 0;    // Label
    EnvPtr parent;
};

namespace {

// One level of evaluation, released on exceptions too.
class DepthGuard {
public:
    explicit DepthGuard(int& depth) : m_depth(depth) {
        if (++m_depth > kJqMaxEvalDepth) {
            --m_depth;
            throw JqError{Value::String(
                "Maximum evaluation depth (" +
                std::to_string(kJqMaxEvalDepth) + ") exceeded")};
        }
    }
    ~DepthGuard() { --m_depth; }

private:
    int& m_depth;
};

// Thrown past a try's body by its own emit wrapper, so that the try can
// tell an error raised inside its body from one raised by whatever
// consumes its outputs downstream: the owning try rethrows it as the plain
// JqError it once was, any other try lets it pass. Not a JqError, so no
// catch of one mistakes it for its own.
struct TryPassThrough {
    uint64_t tryId;
    JqError error;
};

const Env* FindFunction(const EnvPtr& start, const std::string& name, int arity) {
    for (const Env* env = start.get(); env; env = env->parent.get()) {
        if (env->kind == Env::Kind::Function && env->name == name &&
            env->arity == arity)
            return env;
        // A filter parameter is a function of arity 0 where it is bound.
        if (arity == 0 && env->kind == Env::Kind::FilterParam &&
            env->name == name)
            return env;
    }
    return nullptr;
}

const Env* FindVariable(const EnvPtr& start, const std::string& name) {
    for (const Env* env = start.get(); env; env = env->parent.get())
        if (env->kind == Env::Kind::Variable && env->name == name)
            return env;
    return nullptr;
}

const Env* FindLabel(const EnvPtr& start, const std::string& name) {
    for (const Env* env = start.get(); env; env = env->parent.get())
        if (env->kind == Env::Kind::Label && env->name == name)
            return env;
    return nullptr;
}

EnvPtr MakeFilterParamEnv(const EnvPtr& parent, std::string name,
                          Closure closure) {
    auto env = std::make_shared<Env>();
    env->kind = Env::Kind::FilterParam;
    env->name = std::move(name);
    env->closure = std::move(closure);
    env->parent = parent;
    return env;
}

// Every variable a pattern (or its nested patterns) binds, in order.
void CollectPatternVariables(const Pattern& pattern,
                             std::vector<std::string>& out) {
    switch (pattern.type) {
        case PatternType::Variable:
            out.push_back(pattern.name);
            break;
        case PatternType::Array:
            for (const Pattern& element : pattern.elements)
                CollectPatternVariables(element, out);
            break;
        case PatternType::Object:
            for (const auto& entry : pattern.entries) {
                if (!entry.variable.empty())
                    out.push_back(entry.variable);
                if (entry.value)
                    CollectPatternVariables(*entry.value, out);
            }
            break;
    }
}

// A source output bound to a pattern: every binding set it allows, each
// handed to |next| (an object pattern's key may be an expression with
// several outputs). Indexing failures throw the usual JqError.
void MatchPattern(Interpreter& interp, const Pattern& pattern,
                  const Value& value, const EnvPtr& env,
                  const std::function<void(const EnvPtr&)>& next) {
    switch (pattern.type) {
        case PatternType::Variable:
            next(MakeVariableEnv(env, pattern.name, value));
            return;
        case PatternType::Array: {
            // Element i of the value, left to right, each binding extending
            // the ones before it.
            const size_t count = pattern.elements.size();
            std::function<void(size_t, const EnvPtr&)> element =
                [&](size_t i, const EnvPtr& bound) {
                    if (i == count) {
                        next(bound);
                        return;
                    }
                    const Value item = IndexValue(
                        value, Value::Number(static_cast<double>(i)));
                    MatchPattern(interp, pattern.elements[i], item, bound,
                                  [&](const EnvPtr& withElement) {
                                      element(i + 1, withElement);
                                  });
                };
            element(0, env);
            return;
        }
        case PatternType::Object: {
            // Entry by entry, the key evaluated on the value being
            // destructured (each of its outputs a binding set of its own).
            const size_t count = pattern.entries.size();
            std::function<void(size_t, const EnvPtr&)> entry =
                [&](size_t i, const EnvPtr& bound) {
                    if (i == count) {
                        next(bound);
                        return;
                    }
                    const Pattern::Entry& thisEntry = pattern.entries[i];
                    auto bindMember = [&](const Value& member,
                                          const EnvPtr& e) {
                        EnvPtr withVar =
                            thisEntry.variable.empty()
                                ? e
                                : MakeVariableEnv(e, thisEntry.variable,
                                                  member);
                        if (thisEntry.value) {
                            MatchPattern(interp, *thisEntry.value, member,
                                         withVar,
                                         [&](const EnvPtr& withValue) {
                                             entry(i + 1, withValue);
                                         });
                        } else {
                            entry(i + 1, withVar);
                        }
                    };
                    if (thisEntry.key) {
                        interp.Eval(*thisEntry.key, value, bound,
                                    [&](const Value& key) {
                                        bindMember(IndexValue(value, key),
                                                   bound);
                                    });
                    } else {
                        // "$a": the key is the variable's own name.
                        bindMember(
                            IndexValue(value,
                                       Value::String(thisEntry.variable)),
                            bound);
                    }
                };
            entry(0, env);
            return;
        }
    }
}

} // namespace

EnvPtr MakeVariableEnv(const EnvPtr& parent, std::string name, Value value) {
    auto env = std::make_shared<Env>();
    env->kind = Env::Kind::Variable;
    env->name = std::move(name);
    env->value = std::move(value);
    env->parent = parent;
    return env;
}

EnvPtr MakeFunctionEnv(const EnvPtr& parent,
                       const FunctionDefinition& definition) {
    auto env = std::make_shared<Env>();
    env->kind = Env::Kind::Function;
    env->name = definition.name;
    env->arity = static_cast<int>(definition.params.size());
    env->definition = &definition;
    env->parent = parent;
    // The definition sees itself (recursion): the env it runs in is the one
    // holding its own binding, weakly so the two do not keep each other.
    env->definitionEnv = env;
    return env;
}

EnvPtr MakeLabelEnv(const EnvPtr& parent, std::string name, uint64_t label) {
    auto env = std::make_shared<Env>();
    env->kind = Env::Kind::Label;
    env->name = std::move(name);
    env->label = label;
    env->parent = parent;
    return env;
}

namespace {

// One source output destructured under the ?// alternatives: alternative
// |index| first, and an error raised while destructuring or by |runBound|
// under it moves on to the next (the variables all back to null, the
// outputs already emitted staying); the last alternative's error
// propagates.
void DestructureAlternatives(Interpreter& interp,
                             const std::vector<Pattern>& patterns,
                             size_t index, const Value& source,
                             const EnvPtr& base,
                             const std::function<void(const EnvPtr&)>& runBound) {
    try {
        MatchPattern(interp, patterns[index], source, base, runBound);
    } catch (JqError&) {
        if (index + 1 < patterns.size())
            DestructureAlternatives(interp, patterns, index + 1, source,
                                    base, runBound);
        else
            throw;
    }
}

// The value of a Literal node's text (the compile-time cache holds it; a
// miss builds it).
} // namespace

Value LiteralValueOf(const std::string& text) {
    if (text == "true")
        return Value::Boolean(true);
    if (text == "false")
        return Value::Boolean(false);
    if (text == "null")
        return Value();
    std::string canonical;
    double value = 0.0;
    if (!CanonicalNumberLiteral(text, canonical, value)) {
        assert(false && "a Literal's text is a number, true, false or null");
        return Value();
    }
    if (canonical.empty())
        return Value::Number(value);
    return Value::NumberLiteral(value, std::move(canonical));
}

namespace {

// A binary operator on its two values (the right operand was evaluated
// outer, its value first).
Value BinaryResult(const std::string& op, const Value& left,
                   const Value& right) {
    if (op == "+")
        return Add(left, right);
    if (op == "-")
        return Subtract(left, right);
    if (op == "*")
        return Multiply(left, right);
    if (op == "/")
        return Divide(left, right);
    if (op == "%")
        return Modulo(left, right);
    if (op == "==")
        return Value::Boolean(Equal(left, right));
    if (op == "!=")
        return Value::Boolean(!Equal(left, right));
    const int order = Compare(left, right);
    if (op == "<")
        return Value::Boolean(order < 0);
    if (op == "<=")
        return Value::Boolean(order <= 0);
    if (op == ">")
        return Value::Boolean(order > 0);
    return Value::Boolean(order >= 0);
}

// The members an object construction collected, a repeated key keeping its
// first place and its last value.
std::vector<ObjectEntry> UniqueMembers(std::vector<ObjectEntry> collected) {
    std::vector<ObjectEntry> unique;
    std::unordered_map<std::string, size_t> position;
    for (ObjectEntry& entry : collected) {
        const auto found = position.find(entry.first);
        if (found == position.end()) {
            position.emplace(entry.first, unique.size());
            unique.push_back(std::move(entry));
        } else {
            unique[found->second].second = std::move(entry.second);
        }
    }
    return unique;
}

// "Cannot use <kind> (<dump 15>) as object key" -- the runtime form of the
// compile-time check's message, for the keys jq computes from constants.
[[noreturn]] void FailObjectKey(const Value& key) {
    std::string message = "Cannot use ";
    message += KindName(key.GetKind());
    message += " (";
    message += DumpTruncated(key, 15);
    message += ") as object key";
    throw JqError{Value::String(std::move(message))};
}

// A user-defined function called: its $value parameters evaluated on the
// input, the first outermost, their arguments' combinations in jq's order;
// the filter parameters passed as closures over the caller's env; the body
// run in the definition's env (which holds the function itself, so it may
// recurse) extended with the parameters.
void CallUser(Interpreter& interp, const Env& binding, const Node& node,
              const Value& input, const EnvPtr& callerEnv, const Emit& emit) {
    const FunctionDefinition& definition = *binding.definition;
    const EnvPtr defEnv = binding.definitionEnv.lock();
    std::vector<size_t> valueParams;
    for (size_t i = 0; i < definition.params.size(); ++i) {
        if (!definition.params[i].empty() && definition.params[i][0] == '$')
            valueParams.push_back(i);
    }
    std::vector<Value> values;
    std::function<void(size_t)> step = [&](size_t k) {
        if (k == valueParams.size()) {
            EnvPtr bodyEnv = defEnv;
            size_t valueIndex = 0;
            for (size_t i = 0; i < definition.params.size(); ++i) {
                const std::string& param = definition.params[i];
                if (!param.empty() && param[0] == '$') {
                    const std::string name = param.substr(1);
                    // Bound as $name, and as the filter name -- the
                    // argument itself, run again where it is called, as the
                    // manual's 'def f(a): a as $a | ...' reading has it
                    // (def f($a): a; [f(1,2)] is [1,2,1,2]).
                    bodyEnv = MakeFilterParamEnv(
                        bodyEnv, name, Closure{node.children[i].get(), callerEnv});
                    bodyEnv = MakeVariableEnv(bodyEnv, name, values[valueIndex]);
                    ++valueIndex;
                } else {
                    bodyEnv = MakeFilterParamEnv(
                        bodyEnv, param,
                        Closure{node.children[i].get(), callerEnv});
                }
            }
            interp.Eval(*definition.body, input, bodyEnv, emit);
            return;
        }
        const size_t i = valueParams[k];
        interp.Eval(*node.children[i], input, callerEnv,
                    [&](const Value& value) {
                        values.push_back(value);
                        step(k + 1);
                        values.pop_back();
                    });
    };
    step(0);
}

} // namespace

Interpreter::Interpreter(const NativeRegistry& natives, JqHost& host,
                         const std::unordered_map<const Node*, Value>* literals)
    : m_natives(natives), m_host(host), m_literals(literals) {}

JqHost& Interpreter::Host() {
    return m_host;
}

void Interpreter::CheckStop() {
    if (m_host.StopRequested())
        throw JqStopped{};
}

void Interpreter::Eval(const Node& node, const Value& input, const EnvPtr& env,
                       const Emit& emit) {
    DepthGuard guard(m_depth);
    switch (node.type) {
        case NodeType::Identity:
            emit(input);
            return;
        case NodeType::RecurseDefault:
            EvalRecurseDefault(input, emit);
            return;
        case NodeType::Index:
            EvalIndex(node, input, env, emit);
            return;
        case NodeType::Slice:
            EvalSlice(node, input, env, emit);
            return;
        case NodeType::Iterate:
            EvalIterate(node, input, env, emit);
            return;
        case NodeType::Try:
            EvalTry(node, input, env, emit);
            return;
        case NodeType::Literal:
            EvalLiteral(node, emit);
            return;
        case NodeType::String:
            EvalString(node, input, env, emit);
            return;
        case NodeType::Format:
            EvalFormat(node, input, emit);
            return;
        case NodeType::Array:
            EvalArray(node, input, env, emit);
            return;
        case NodeType::Object:
            EvalObject(node, input, env, emit);
            return;
        case NodeType::Negate:
            EvalNegate(node, input, env, emit);
            return;
        case NodeType::Pipe:
            Eval(*node.children[0], input, env, [&](const Value& output) {
                Eval(*node.children[1], output, env, emit);
            });
            return;
        case NodeType::Comma:
            Eval(*node.children[0], input, env, emit);
            Eval(*node.children[1], input, env, emit);
            return;
        case NodeType::Alternative:
            EvalAlternative(node, input, env, emit);
            return;
        case NodeType::And:
        case NodeType::Or:
            EvalAndOr(node, input, env, emit);
            return;
        case NodeType::Binary:
            EvalBinary(node, input, env, emit);
            return;
        case NodeType::Assign:
            // tools--jq-paths implements assignment.
            throw JqError{Value::String("assignment is not implemented yet")};
        case NodeType::If:
            EvalIf(node, input, env, emit);
            return;
        case NodeType::Reduce:
            EvalReduce(node, input, env, emit);
            return;
        case NodeType::Foreach:
            EvalForeach(node, input, env, emit);
            return;
        case NodeType::Bind:
            EvalBind(node, input, env, emit);
            return;
        case NodeType::Defs:
            EvalDefs(node, input, env, emit);
            return;
        case NodeType::Call:
            EvalCall(node, input, env, emit);
            return;
        case NodeType::Variable: {
            const Env* binding = FindVariable(env, node.text);
            assert(binding && "the compile check guarantees the variable");
            emit(binding->value);
            return;
        }
        case NodeType::Loc:
            emit(Value::Object({
                {"file", Value::String("<top-level>")},
                {"line", Value::Number(node.line)},
            }));
            return;
        case NodeType::Label:
            EvalLabel(node, input, env, emit);
            return;
        case NodeType::Break: {
            const Env* binding = FindLabel(env, node.text);
            assert(binding && "the compile check guarantees the label");
            throw JqBreak{binding->label};
        }
    }
}

void Interpreter::EvalClosure(const Closure& closure, const Value& input,
                              const Emit& emit) {
    Eval(*closure.body, input, closure.env, emit);
}

void Interpreter::EvalRecurseDefault(const Value& input, const Emit& emit) {
    // The input, then every element and member value below it, pre-order,
    // objects in insertion order -- iteratively: a value nested deeper
    // than the evaluation limit can still be walked.
    std::vector<Value> pending;
    pending.push_back(input);
    while (!pending.empty()) {
        CheckStop();
        const Value value = pending.back();
        pending.pop_back();
        emit(value);
        if (value.GetKind() == Kind::Array) {
            const std::vector<Value>& elements = value.AsArray();
            for (auto it = elements.rbegin(); it != elements.rend(); ++it)
                pending.push_back(*it);
        } else if (value.GetKind() == Kind::Object) {
            const std::vector<ObjectEntry>& members = value.AsObject();
            for (auto it = members.rbegin(); it != members.rend(); ++it)
                pending.push_back(it->second);
        }
    }
}

void Interpreter::EvalIndex(const Node& node, const Value& input,
                           const EnvPtr& env, const Emit& emit) {
    // The key outer, the target inner.
    Eval(*node.children[1], input, env, [&](const Value& key) {
        Eval(*node.children[0], input, env, [&](const Value& target) {
            emit(IndexValue(target, key));
        });
    });
}

void Interpreter::EvalSlice(const Node& node, const Value& input,
                            const EnvPtr& env, const Emit& emit) {
    // From outer, then to, then the target.
    auto withTarget = [&](const Value& from, const Value& to) {
        Eval(*node.children[0], input, env, [&](const Value& target) {
            emit(SliceValue(target, from, to));
        });
    };
    auto withFrom = [&](const Value& from) {
        if (node.children[2])
            Eval(*node.children[2], input, env,
                 [&](const Value& to) { withTarget(from, to); });
        else
            withTarget(from, Value());
    };
    if (node.children[1])
        Eval(*node.children[1], input, env, withFrom);
    else if (node.children[2])
        Eval(*node.children[2], input, env,
             [&](const Value& to) { withTarget(Value(), to); });
    else
        withTarget(Value(), Value());
}

void Interpreter::EvalIterate(const Node& node, const Value& input,
                              const EnvPtr& env, const Emit& emit) {
    Eval(*node.children[0], input, env, [&](const Value& target) {
        IterateValue(target, [&](const Value& element) {
            CheckStop();
            emit(element);
        });
    });
}

void Interpreter::EvalTry(const Node& node, const Value& input,
                         const EnvPtr& env, const Emit& emit) {
    // Only errors raised inside the body are caught -- not those raised by
    // whatever consumes its outputs downstream: the body's emit wraps such
    // a downstream error in a pass-through that names this try, which
    // rethrows it as the plain error it once was (and lets every other
    // try's pass-through by untouched). break and a stop are never caught.
    const uint64_t tryId = m_nextTryId++;
    try {
        Eval(*node.children[0], input, env, [this, &emit, tryId](const Value& v) {
            try {
                emit(v);
            } catch (JqError& error) {
                throw TryPassThrough{tryId, error};
            }
        });
    } catch (TryPassThrough& passThrough) {
        if (passThrough.tryId != tryId)
            throw;  // another try's downstream error: not ours to catch
        throw passThrough.error;
    } catch (JqError& error) {
        // The body's own error: it is not resumed; the catch body (if any)
        // runs on the error's value.
        if (node.hasCatch)
            Eval(*node.children[1], error.value, env, emit);
    }
}

void Interpreter::EvalLiteral(const Node& node, const Emit& emit) {
    if (m_literals) {
        const auto found = m_literals->find(&node);
        if (found != m_literals->end()) {
            emit(found->second);
            return;
        }
    }
    emit(LiteralValueOf(node.text));
}

void Interpreter::EvalString(const Node& node, const Value& input,
                             const EnvPtr& env, const Emit& emit) {
    if (node.children.empty()) {
        // A string with a format and no interpolation is its text, the
        // format unused.
        emit(Value::String(node.stringParts.empty()
                               ? std::string()
                               : node.stringParts[0]));
        return;
    }
    const std::string& format = node.text;  // may be "": tostring
    // The last interpolation outermost.
    std::function<void(size_t, std::string)> step =
        [&](size_t k, std::string suffix) {
            Eval(*node.children[k], input, env, [&](const Value& v) {
                const std::string piece =
                    ApplyFormat(format.empty() ? "text" : format, v)
                        .AsString();
                if (k == 0) {
                    emit(Value::String(node.stringParts[0] + piece +
                                       std::move(suffix)));
                } else {
                    step(k - 1,
                         node.stringParts[k] + piece + std::move(suffix));
                }
            });
        };
    step(node.children.size() - 1, node.stringParts.back());
}

void Interpreter::EvalFormat(const Node& node, const Value& input,
                            const Emit& emit) {
    emit(ApplyFormat(node.text, input));
}

void Interpreter::EvalArray(const Node& node, const Value& input,
                           const EnvPtr& env, const Emit& emit) {
    if (node.children.empty() || !node.children[0]) {
        emit(Value::Array({}));
        return;
    }
    std::vector<Value> elements;
    Eval(*node.children[0], input, env,
         [&](const Value& v) { elements.push_back(v); });
    emit(Value::Array(std::move(elements)));
}

void Interpreter::EvalObject(const Node& node, const Value& input,
                             const EnvPtr& env, const Emit& emit) {
    // The entries left to right, the first outermost; within an entry the
    // key outer, the value inner. The members are collected with a local
    // key index, then one Value::Object (UniqueMembers).
    std::vector<ObjectEntry> members;
    std::function<void(size_t)> entry = [&](size_t i) {
        if (i == node.entries.size()) {
            emit(Value::Object(UniqueMembers(members)));
            return;
        }
        Eval(*node.entries[i].first, input, env, [&](const Value& key) {
            if (key.GetKind() != Kind::String)
                FailObjectKey(key);
            Eval(*node.entries[i].second, input, env, [&](const Value& value) {
                members.emplace_back(key.AsString(), value);
                entry(i + 1);
                members.pop_back();  // the next combination starts afresh
            });
        });
    };
    entry(0);
}

void Interpreter::EvalNegate(const Node& node, const Value& input,
                             const EnvPtr& env, const Emit& emit) {
    Eval(*node.children[0], input, env, [&](const Value& v) {
        if (v.GetKind() == Kind::Number) {
            emit(Value::Number(-v.AsNumber()));
            return;
        }
        std::string message = KindName(v.GetKind());
        message += " (";
        message += DumpTruncated(v, 15);
        message += ") cannot be negated";
        throw JqError{Value::String(std::move(message))};
    });
}

void Interpreter::EvalAlternative(const Node& node, const Value& input,
                                 const EnvPtr& env, const Emit& emit) {
    // A // B: every truthy output of A, or -- when A yields none that is
    // truthy (or errors are raised only by outputs no one consumes yet) --
    // every output of B.
    bool anyTruthy = false;
    Eval(*node.children[0], input, env, [&](const Value& left) {
        if (left.IsTruthy()) {
            anyTruthy = true;
            emit(left);
        }
    });
    if (!anyTruthy)
        Eval(*node.children[1], input, env, emit);
}

void Interpreter::EvalAndOr(const Node& node, const Value& input,
                            const EnvPtr& env, const Emit& emit) {
    // The left outer; a short-circuiting side settles each of its outputs,
    // the other side's truth per output of its own.
    const bool isAnd = node.type == NodeType::And;
    const bool decides = isAnd ? false : true;
    Eval(*node.children[0], input, env, [&](const Value& left) {
        if (left.IsTruthy() == decides) {
            emit(Value::Boolean(decides));
            return;
        }
        Eval(*node.children[1], input, env, [&](const Value& right) {
            emit(Value::Boolean(right.IsTruthy()));
        });
    });
}

void Interpreter::EvalBinary(const Node& node, const Value& input,
                             const EnvPtr& env, const Emit& emit) {
    // The right operand outer, its value first.
    Eval(*node.children[1], input, env, [&](const Value& right) {
        Eval(*node.children[0], input, env, [&](const Value& left) {
            emit(BinaryResult(node.text, left, right));
        });
    });
}

void Interpreter::EvalIf(const Node& node, const Value& input,
                         const EnvPtr& env, const Emit& emit) {
    // [cond, then, cond, then, ..., else-if-given]; a falsy condition falls
    // through to the next, a missing else keeps the input.
    const size_t pairs = (node.children.size() - (node.hasElse ? 1 : 0)) / 2;
    std::function<void(size_t)> decide = [&](size_t p) {
        if (p == pairs) {
            if (node.hasElse)
                Eval(*node.children.back(), input, env, emit);
            else
                emit(input);
            return;
        }
        Eval(*node.children[2 * p], input, env, [&](const Value& condition) {
            if (condition.IsTruthy())
                Eval(*node.children[2 * p + 1], input, env, emit);
            else
                decide(p + 1);
        });
    };
    decide(0);
}

void Interpreter::EvalReduce(const Node& node, const Value& input,
                             const EnvPtr& env, const Emit& emit) {
    // [source, init, update]: one state per init output, the source walked
    // again for each of them, the update run on the state per source output
    // -- its last output the new state, null when it gave none.
    std::vector<std::string> names;
    for (const Pattern& pattern : node.patterns) {
        std::vector<std::string> more;
        CollectPatternVariables(pattern, more);
        names.insert(names.end(), more.begin(), more.end());
    }
    Eval(*node.children[1], input, env, [&](const Value& start) {
        Value state = start;
        Eval(*node.children[0], input, env, [&](const Value& item) {
            CheckStop();
            EnvPtr base = env;
            for (const std::string& name : names)
                base = MakeVariableEnv(base, name, Value());
            DestructureAlternatives(*this, node.patterns, 0, item, base,
                                    [&](const EnvPtr& bound) {
                                        bool had = false;
                                        Value last;
                                        Eval(*node.children[2], state, bound,
                                             [&](const Value& updated) {
                                                 had = true;
                                                 last = updated;
                                             });
                                        state = had ? last : Value();
                                    });
        });
        emit(state);
    });
}

void Interpreter::EvalForeach(const Node& node, const Value& input,
                              const EnvPtr& env, const Emit& emit) {
    // [source, init, update, extract-if-given]: like reduce, but every
    // update output passed on -- through the extract, on the bound env --
    // before the walk moves on.
    std::vector<std::string> names;
    for (const Pattern& pattern : node.patterns) {
        std::vector<std::string> more;
        CollectPatternVariables(pattern, more);
        names.insert(names.end(), more.begin(), more.end());
    }
    const bool hasExtract = node.children.size() > 3;
    Eval(*node.children[1], input, env, [&](const Value& start) {
        Value state = start;
        Eval(*node.children[0], input, env, [&](const Value& item) {
            CheckStop();
            EnvPtr base = env;
            for (const std::string& name : names)
                base = MakeVariableEnv(base, name, Value());
            DestructureAlternatives(*this, node.patterns, 0, item, base,
                                    [&](const EnvPtr& bound) {
                                        bool had = false;
                                        Value last;
                                        Eval(*node.children[2], state, bound,
                                             [&](const Value& updated) {
                                                 had = true;
                                                 last = updated;
                                                 if (hasExtract) {
                                                     Eval(*node.children[3],
                                                          updated, bound,
                                                          emit);
                                                 } else {
                                                     emit(updated);
                                                 }
                                             });
                                        state = had ? last : Value();
                                    });
        });
    });
}

void Interpreter::EvalBind(const Node& node, const Value& input,
                           const EnvPtr& env, const Emit& emit) {
    // [source, body]: every source output bound to the pattern, the body run
    // on the original input under each binding set.
    std::vector<std::string> names;
    for (const Pattern& pattern : node.patterns) {
        std::vector<std::string> more;
        CollectPatternVariables(pattern, more);
        names.insert(names.end(), more.begin(), more.end());
    }
    Eval(*node.children[0], input, env, [&](const Value& source) {
        EnvPtr base = env;
        for (const std::string& name : names)
            base = MakeVariableEnv(base, name, Value());
        DestructureAlternatives(*this, node.patterns, 0, source, base,
                                [&](const EnvPtr& bound) {
                                    Eval(*node.children[1], input, bound,
                                         emit);
                                });
    });
}

void Interpreter::EvalDefs(const Node& node, const Value& input,
                           const EnvPtr& env, const Emit& emit) {
    // The definitions scope over the rest; each sees itself (recursion) and
    // the ones before it.
    EnvPtr scope = env;
    for (const FunctionDefinition& definition : node.definitions)
        scope = MakeFunctionEnv(scope, definition);
    Eval(*node.children[0], input, scope, emit);
}

void Interpreter::EvalCall(const Node& node, const Value& input,
                           const EnvPtr& env, const Emit& emit) {
    CheckStop();
    const int arity = static_cast<int>(node.children.size());
    const Env* binding = FindFunction(env, node.text, arity);
    if (binding) {
        if (binding->kind == Env::Kind::FilterParam) {
            EvalClosure(binding->closure, input, emit);
            return;
        }
        CallUser(*this, *binding, node, input, env, emit);
        return;
    }
    const NativeFunction* native = m_natives.Find(node.text, arity);
    assert(native && "the compile check guarantees the function");
    std::vector<Closure> arguments;
    arguments.reserve(node.children.size());
    for (const auto& argument : node.children)
        arguments.push_back(Closure{argument.get(), env});
    native->run(*this, input, arguments, emit);
}

void Interpreter::EvalLabel(const Node& node, const Value& input,
                           const EnvPtr& env, const Emit& emit) {
    // A break of this label ends the body's run quietly (the outputs already
    // emitted stay); a break of another label passes through.
    const uint64_t label = m_nextLabel++;
    try {
        Eval(*node.children[0], input, MakeLabelEnv(env, node.text, label),
             emit);
    } catch (JqBreak& broken) {
        if (broken.label != label)
            throw;
    }
}

} // namespace Haisos::Jq