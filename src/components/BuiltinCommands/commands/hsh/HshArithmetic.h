#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace Haisos::Hsh {

// The variables $((...)) reads and assigns; the executor implements it over
// the shell's variables.
class IArithmeticVariables {
public:
    virtual ~IArithmeticVariables() = default;
    // The variable's value; nullopt when it is unset.
    virtual std::optional<std::string> Get(const std::string& name) = 0;
    // Sets it; false (changing nothing) when it is read-only.
    virtual bool Set(const std::string& name, const std::string& value) = 0;
};

// Evaluates |expression| -- the text of a $((...)) after its own parameter
// expansions and command substitutions -- as dash does: intmax_t with
// wrapping two's-complement arithmetic, dash's operators and precedence,
// assignments (decimal) through |variables|, short-circuit of &&, || and ?:
// (the branch not taken is parsed but never looked up, assigned or divided).
// Throws ShellError (line 0) on an error, with dash's messages:
// "arithmetic expression: <what>: \"<expression>\"" for syntax errors and
// division by zero, "Illegal number: <value>" for a variable value that is
// not a number, "<name>: is read only" for assigning a read-only variable.
intmax_t EvaluateArithmetic(const std::string& expression, IArithmeticVariables& variables);

} // namespace Haisos::Hsh
