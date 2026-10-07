#include <gtest/gtest.h>

#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "commands/hsh/HshArithmetic.h"
#include "commands/hsh/HshError.h"

using namespace Haisos::Hsh;

namespace {

class FakeArithmeticVariables : public IArithmeticVariables {
public:
    std::map<std::string, std::string> values;
    std::set<std::string> readOnly;

    std::optional<std::string> Get(const std::string& name) override {
        auto it = values.find(name);
        if (it == values.end()) return std::nullopt;
        return it->second;
    }

    bool Set(const std::string& name, const std::string& value) override {
        if (readOnly.count(name) != 0) return false;
        values[name] = value;
        return true;
    }
};

constexpr intmax_t kMax = std::numeric_limits<intmax_t>::max();
constexpr intmax_t kMin = std::numeric_limits<intmax_t>::min();

TEST(HshArithmeticTest, Values) {
    const std::vector<std::pair<std::string, intmax_t>> rows = {
        {"1+2", 3}, {"3/2", 1}, {"-7/2", -3}, {"-7%2", -1}, {"7 % -3", 1},
        {"1<<3", 8}, {"5&3", 1}, {"5|3", 7}, {"5^3", 6}, {"~0", -1}, {"!3", 0},
        {"2>1", 1}, {"1&&0", 0}, {"0||2", 1}, {"1 < 2 < 3", 1},
        {"(1+2)*3", 9}, {"010", 8}, {"0X10", 16}, {"0x1f", 31},
        {"--1", 1}, {"1 +-+ 2", -1}, {"!0 + ~1", -1},
        {"1 ? 2 : 3 ? 4 : 5", 2}, {"0 ? 1 : 0 ? 4 : 5", 5},
        {"99999999999999999999", kMax}, {"-9223372036854775808", kMin + 1},
        {"9223372036854775807 + 1", kMin}, {"1 << 64", 1}, {"1<<63", kMin},
        {"-1 >> 1", -1}, {"(0-9223372036854775807-1) / -1", kMin},
        {"(0-9223372036854775807-1) % -1", 0},
    };
    for (const auto& [expression, expected] : rows) {
        FakeArithmeticVariables variables;
        EXPECT_EQ(EvaluateArithmetic(expression, variables), expected) << "expression: " << expression;
    }
}

TEST(HshArithmeticTest, Variables) {
    FakeArithmeticVariables variables;
    EXPECT_EQ(EvaluateArithmetic("novar", variables), 0);
    variables.values["empty"] = "";
    EXPECT_EQ(EvaluateArithmetic("empty", variables), 0);
    variables.values["spaced"] = " 12 ";
    EXPECT_EQ(EvaluateArithmetic("spaced", variables), 12);
    variables.values["hex"] = " -0x1f ";
    EXPECT_EQ(EvaluateArithmetic("hex", variables), -31);
    variables.values["plus"] = "+5";
    EXPECT_EQ(EvaluateArithmetic("plus", variables), 5);
    variables.values["octal"] = "010";
    EXPECT_EQ(EvaluateArithmetic("octal", variables), 8);

    EXPECT_EQ(EvaluateArithmetic("x = 3", variables), 3);
    EXPECT_EQ(variables.values["x"], "3");
    EXPECT_EQ(EvaluateArithmetic("x = y = 3", variables), 3);
    EXPECT_EQ(variables.values["x"], "3");
    EXPECT_EQ(variables.values["y"], "3");

    variables.values["x"] = "5";
    const std::vector<std::pair<std::string, intmax_t>> compounds = {
        {"x += 2", 7}, {"x -= 1", 6}, {"x *= 2", 12}, {"x /= 3", 4}, {"x %= 3", 1},
        {"x <<= 2", 4}, {"x >>= 1", 2}, {"x &= 7", 2}, {"x |= 8", 10}, {"x ^= 1", 11},
    };
    for (const auto& [expression, expected] : compounds) {
        EXPECT_EQ(EvaluateArithmetic(expression, variables), expected) << "expression: " << expression;
        EXPECT_EQ(variables.values["x"], std::to_string(expected)) << "expression: " << expression;
    }

    variables.values.erase("y");
    EXPECT_EQ(EvaluateArithmetic("y = y + 1", variables), 1);
    EXPECT_EQ(variables.values["y"], "1");

    variables.values.erase("x");
    EXPECT_EQ(EvaluateArithmetic("0 && (x = 5)", variables), 0);
    EXPECT_EQ(variables.values.count("x"), 0u); // the branch not taken assigns nothing
    EXPECT_EQ(EvaluateArithmetic("1 || 1/0", variables), 1);
    EXPECT_EQ(EvaluateArithmetic("0 ? 1/0 : 2", variables), 2);
    EXPECT_EQ(EvaluateArithmetic("0 && 1/0", variables), 0);

    // Assignment reads the variable at the right time: a plain '=' never
    // reads the old value, a compound operator reads it only after the
    // right-hand side has been evaluated (dash).
    variables.values["x"] = "abc";
    EXPECT_EQ(EvaluateArithmetic("x=3", variables), 3); // not "Illegal number: abc"
    EXPECT_EQ(variables.values["x"], "3");
    variables.values["x"] = "5";
    EXPECT_EQ(EvaluateArithmetic("x += (x=2)", variables), 4);
    EXPECT_EQ(variables.values["x"], "4");
}

void ExpectArithError(const std::string& expression, IArithmeticVariables& variables,
                      const std::string& message) {
    try {
        EvaluateArithmetic(expression, variables);
        FAIL() << "expected an error for: " << expression;
    } catch (const ShellError& e) {
        EXPECT_EQ(std::string(e.what()), message) << "expression: " << expression;
        EXPECT_EQ(e.Line(), 0);
    }
}

TEST(HshArithmeticTest, Errors) {
    FakeArithmeticVariables variables;
    ExpectArithError("1/0", variables, "arithmetic expression: division by zero: \"1/0\"");
    ExpectArithError("5%0", variables, "arithmetic expression: division by zero: \"5%0\"");

    for (const std::string& expression : {"1 +", "", "  ", "@", " 2 ** 3 ", "a++", "x--"})
        ExpectArithError(expression, variables,
            "arithmetic expression: expecting primary: \"" + expression + "\"");

    for (const std::string& expression : {"a b c", "08", "1.5", "x[1]", "3=4", "1,2", " 0x ", "0x", "0xg"})
        ExpectArithError(expression, variables,
            "arithmetic expression: expecting EOF: \"" + expression + "\"");

    ExpectArithError("(1", variables, "arithmetic expression: expecting ')': \"(1\"");
    ExpectArithError(" 1 ? 2 ", variables, "arithmetic expression: expecting ':': \" 1 ? 2 \"");

    for (const std::string& value : {"abc", "1+2", "12 a", "09"}) {
        FakeArithmeticVariables bad;
        bad.values["v"] = value;
        ExpectArithError("v", bad, "Illegal number: " + value);
    }

    variables.values["r"] = "1";
    variables.readOnly.insert("r");
    ExpectArithError("r = 2", variables, "r: is read only");
}

} // namespace
