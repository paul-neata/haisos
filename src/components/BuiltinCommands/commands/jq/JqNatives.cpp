#include "commands/jq/JqNatives.h"

#include <algorithm>

#include "commands/jq/JqValue.h"

namespace Haisos::Jq {

void NativeRegistry::Add(NativeFunction function) {
    for (NativeFunction& existing : m_functions) {
        if (existing.name == function.name && existing.arity == function.arity) {
            existing = std::move(function);
            return;
        }
    }
    m_functions.push_back(std::move(function));
}

const NativeFunction* NativeRegistry::Find(const std::string& name,
                                            int arity) const {
    for (const NativeFunction& function : m_functions) {
        if (function.name == name && function.arity == arity)
            return &function;
    }
    return nullptr;
}

std::vector<std::string> NativeRegistry::Names() const {
    std::vector<std::string> names;
    names.reserve(m_functions.size());
    for (const NativeFunction& function : m_functions)
        names.push_back(function.name + "/" + std::to_string(function.arity));
    std::sort(names.begin(), names.end());
    return names;
}

void ForEachArgumentCombination(
    Interpreter& interp, const Value& input, const std::vector<Closure>& arguments,
    const std::function<void(const std::vector<Value>&)>& each) {
    // The last argument outermost: argument 0's outputs vary slowest, and
    // an argument's whole run precedes the next one's first output.
    std::vector<Value> values(arguments.size());
    if (arguments.empty()) {
        each(values);
        return;
    }
    std::function<void(size_t)> step = [&](size_t k) {
        interp.EvalClosure(arguments[k], input, [&](const Value& v) {
            values[k] = v;
            if (k == 0)
                each(values);
            else
                step(k - 1);
        });
    };
    step(arguments.size() - 1);
}

void RegisterCoreNatives(NativeRegistry& registry) {
    registry.Add(NativeFunction{
        "empty", 0,
        [](Interpreter&, const Value&, const std::vector<Closure>&,
           const Emit&) {},
        {}});
    registry.Add(NativeFunction{
        "error", 0,
        [](Interpreter&, const Value& input, const std::vector<Closure>&,
           const Emit&) { throw JqError{input}; },
        {}});
    registry.Add(NativeFunction{
        "error", 1,
        [](Interpreter& interp, const Value& input,
           const std::vector<Closure>& arguments, const Emit&) {
            // Every output of the message is the error, one at a time.
            interp.EvalClosure(arguments[0], input, [&](const Value& message) {
                throw JqError{message};
            });
        },
        {}});
    registry.Add(NativeFunction{
        "not", 0,
        [](Interpreter&, const Value& input, const std::vector<Closure>&,
           const Emit& emit) { emit(Value::Boolean(!input.IsTruthy())); },
        {}});
    registry.Add(NativeFunction{
        "type", 0,
        [](Interpreter&, const Value& input, const std::vector<Closure>&,
           const Emit& emit) { emit(Value::String(KindName(input.GetKind()))); },
        {}});
}

const NativeRegistry& NativeRegistry::Standard() {
    static const NativeRegistry registry = [] {
        NativeRegistry registry;
        RegisterCoreNatives(registry);
        return registry;
    }();
    return registry;
}

} // namespace Haisos::Jq