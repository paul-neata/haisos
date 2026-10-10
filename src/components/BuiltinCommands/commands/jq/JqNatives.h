#pragma once

#include <functional>
#include <string>
#include <vector>

#include "commands/jq/JqInterpreter.h"  // Closure
#include "commands/jq/JqRuntime.h"

namespace Haisos::Jq {

class Interpreter;
struct PathValue;

// A function the evaluator runs directly (the prelude builds the rest of
// jq's library on top of these and the language itself). |run| receives the
// interpreter (for evaluating a filter argument and for the stop check), the
// input, the arguments as closures and the caller's emit; |runPath| is the
// same call in a path expression (tools--jq-paths gives it a body).
struct NativeFunction {
    using Run = std::function<void(Interpreter&, const Value&,
                                    const std::vector<Closure>&, const Emit&)>;
    using RunPath = std::function<void(Interpreter&, const Value&,
                                       const std::vector<Closure>&,
                                       const PathValue&, const Emit&)>;
    std::string name;
    int arity = 0;
    Run run;
    RunPath runPath;
};

// The natives a program may call, looked up by name and arity (arity first,
// so `error/0` and `error/1` are two functions). One registry serves every
// program; the standard one holds the core four.
class NativeRegistry {
public:
    void Add(NativeFunction function);  // replaces the same name/arity
    const NativeFunction* Find(const std::string& name, int arity) const;
    std::vector<std::string> Names() const;  // "name/arity", sorted

    static const NativeRegistry& Standard();  // the core natives

private:
    std::vector<NativeFunction> m_functions;
};

// Every combination of one output per argument, jq's order: the LAST
// argument outermost, so `f(1,2;3,4)` calls f as (1,3), (1,4), (2,3), (2,4)
// -- the first argument's outputs outer. Each argument is evaluated on
// |input|, and each combination is handed to |each| as one value per
// argument, in argument order. An argument list with no arguments calls
// |each| once with an empty vector.
void ForEachArgumentCombination(
    Interpreter& interp, const Value& input,
    const std::vector<Closure>& arguments,
    const std::function<void(const std::vector<Value>&)>& each);

// The core natives: empty/0 (no output), error/0 (the input as the error),
// error/1 (the argument's output as the error), not/0 (the input's truth
// negated) and type/0 (the input's kind as a string).
void RegisterCoreNatives(NativeRegistry& registry);

} // namespace Haisos::Jq