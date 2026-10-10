#include <gtest/gtest.h>
#include <string>

#include "commands/jq/JqParser.h"

namespace {

using Haisos::Jq::DumpNode;
using Haisos::Jq::FormatCompileError;
using Haisos::Jq::FormatCompileErrorCount;
using Haisos::Jq::ParseProgram;

// The dump of a program that must parse.
std::string Dump(const char* program) {
    auto result = ParseProgram(program);
    EXPECT_TRUE(result.errors.empty()) << program;
    return result.root ? DumpNode(*result.root) : std::string();
}

// A program's errors, exactly as jq prints them.
std::string Errors(const char* program) {
    auto result = ParseProgram(program);
    std::string out;
    for (const auto& error : result.errors)
        out += FormatCompileError(program, error);
    out += FormatCompileErrorCount(result.errors.size());
    return out;
}

} // namespace

TEST(JqParserTest, PathsAndPostfix) {
    EXPECT_EQ(Dump("."), ".");
    EXPECT_EQ(Dump(".."), "..");
    EXPECT_EQ(Dump(".a.b"), R"((index (index . "a") "b"))");
    EXPECT_EQ(Dump(".a?.b"), R"((index (try (index . "a")) "b"))");
    EXPECT_EQ(Dump(".\"a\"[0]"), R"((index (index . "a") 0))");
    EXPECT_EQ(Dump(".a.[0]"), R"((index (index . "a") 0))");
    EXPECT_EQ(Dump(".[]?"), "(try (iterate .))");
    EXPECT_EQ(Dump(".[1:]"), "(slice . 1 _)");
    EXPECT_EQ(Dump(".[:2]"), "(slice . _ 2)");
    EXPECT_EQ(Dump(".[1:2]"), "(slice . 1 2)");
    EXPECT_EQ(Dump("$x.a"), R"((index $x "a"))");
    EXPECT_EQ(Dump("1 .a"), R"((index 1 "a"))");
    EXPECT_EQ(Dump("..[0]"), "(index .. 0)");
}

TEST(JqParserTest, Precedence) {
    EXPECT_EQ(Dump("1 + 2 * 3"), "(+ 1 (* 2 3))");
    EXPECT_EQ(Dump("-1 + 2"), "(+ (neg 1) 2)");
    // The unary minus waits for a Mul: -1 * 3 is -(1 * 3).
    EXPECT_EQ(Dump("- 1 * 3"), "(neg (* 1 3))");
    EXPECT_EQ(Dump("1 - 2 + 3"), "(+ (- 1 2) 3)");
    EXPECT_EQ(Dump(".a // .b // .c"),
              R"((// (index . "a") (// (index . "b") (index . "c"))))");
    EXPECT_EQ(Dump("1, 2 | 3"), "(| (, 1 2) 3)");
    EXPECT_EQ(Dump(".a = 1 | .b"),
              "(| (= (index . \"a\") 1) (index . \"b\"))");
    EXPECT_EQ(Dump("1 < 2 and 2 < 3 or false"),
              "(or (and (< 1 2) (< 2 3)) false)");
    EXPECT_EQ(Dump("try 1 + 2"), "(+ (try 1) 2)");
    EXPECT_EQ(Dump("try error(\"x\") catch . | length"),
              R"((| (try (call error "x") .) (call length)))");
    // '?' after 'end' wraps the whole if.
    EXPECT_EQ(Dump("if . then 1 end?"), "(try (if . 1 _))");
    EXPECT_EQ(Dump("1??"), "(try (try 1))");
}

TEST(JqParserTest, Bindings) {
    // 'as' takes the smallest expression, so the comma continues past it.
    EXPECT_EQ(Dump("1, 2 as $x | $x"), "(, 1 (as 2 $x $x))");
    EXPECT_EQ(Dump("1 as $x | 2, 3"), "(as 1 $x (, 2 3))");
    EXPECT_EQ(Dump("1 + 2 as $x | 10, 20"),
              "(+ 1 (as 2 $x (, 10 20)))");
    EXPECT_EQ(Dump("-1 as $x | 2"), "(neg (as 1 $x 2))");
    EXPECT_EQ(Dump(". as [$a, {b: $c, $d}] | $a"),
              "(as . (arr $a (obj (b $c) ($d))) $a)");
    EXPECT_EQ(Dump(".[] as [$a] ?// $a | $a"),
              "(as (iterate .) (?// (arr $a) $a) $a)");
    EXPECT_EQ(Dump("reduce .[] as $x (0; . + $x)"),
              "(reduce (iterate .) $x 0 (+ . $x))");
    EXPECT_EQ(Dump("foreach .[] as $x (0; . + $x; [$x, .])"),
              "(foreach (iterate .) $x 0 (+ . $x) (array (, $x .)))");
    EXPECT_EQ(Dump("label $f | 1, break $f"),
              "(label $f (, 1 (break $f)))");
}

TEST(JqParserTest, Definitions) {
    EXPECT_EQ(Dump("def f: 1; def g(a; $b): a + $b; f | g(1; 2)"),
              "(defs (def f () 1) (def g (a $b) (+ (call a) $b)) "
              "(| (call f) (call g 1 2)))");
    // A def's scope is the rest of its pipe expression.
    EXPECT_EQ(Dump("1 + def f: 2; f"),
              "(+ 1 (defs (def f () 2) (call f)))");
    EXPECT_EQ(Dump("def f: def g: 3; g; f"),
              "(defs (def f () (defs (def g () 3) (call g))) (call f))");
}

TEST(JqParserTest, ConditionalsAndStrings) {
    EXPECT_EQ(Dump("if . then 1 elif .a then 2 end"),
              R"((if . 1 (index . "a") 2 _))");
    EXPECT_EQ(Dump("if . then 1 else 2 end"), "(if . 1 2)");
    EXPECT_EQ(Dump("if . then 1 end"), "(if . 1 _)");
    EXPECT_EQ(Dump("if . then 1 else .x end"),
              R"((if . 1 (index . "x")))");
    // A string with no interpolation and no format dumps as a JSON string.
    EXPECT_EQ(Dump("\"ab\""), "\"ab\"");
    EXPECT_EQ(Dump("\"a\\(1 + 2)b\""), R"((string _ "a" (+ 1 2) "b"))");
    EXPECT_EQ(Dump("@base64 \"x\\(.)\""),
              R"jq((string @base64 "x" . ""))jq");
    EXPECT_EQ(Dump("@csv"), "(format @csv)");
    EXPECT_EQ(Dump("\"\\(1,2)\""), R"jq((string _ "" (, 1 2) ""))jq");
}

TEST(JqParserTest, ObjectsDesugared) {
    // Shorthand keys are desugared to lookups of '.', '$x' to its variable,
    // keywords and $__loc__ to their values.
    EXPECT_EQ(Dump("{a, $x, \"b c\", (.k): 1, if: 2, $__loc__}"),
              "(object (\"a\" (index . \"a\")) (\"x\" $x) "
              "(\"b c\" (index . \"b c\")) ((index . \"k\") 1) "
              "(\"if\" 2) (\"__loc__\" (loc 1)))");
    // An object value is a pipe of terms: '|' stays inside, ',' does not.
    EXPECT_EQ(Dump("{a: .b | length}"),
              R"((object ("a" (| (index . "b") (call length)))))");
    EXPECT_EQ(Dump("{a,}"), R"((object ("a" (index . "a"))))");
    EXPECT_EQ(Dump("{a: .b?[0]?}"),
              R"((object ("a" (try (index (try (index . "b")) 0)))))");
    EXPECT_EQ(Dump("{a: - - 1}"),
              R"((object ("a" (neg (neg 1)))))");
}

TEST(JqParserTest, SyntaxErrors) {
    // Each text is jq 1.7.1's own output for the program, byte for byte.
    struct ErrorCase {
        const char* program;
        std::string expected;
    };
    const ErrorCase cases[] = {
        {".a |",
         "jq: error: syntax error, unexpected end of file "
         "(Unix shell quoting issues?) at <top-level>, line 1:\n"
         ".a |   \n"
         "jq: 1 compile error\n"},
        {"{a:}",
         "jq: error: syntax error, unexpected '}' "
         "(Unix shell quoting issues?) at <top-level>, line 1:\n"
         "{a:}   \n"
         "jq: 1 compile error\n"},
        {".a.b)",
         "jq: error: syntax error, unexpected INVALID_CHARACTER, expecting "
         "end of file (Unix shell quoting issues?) at <top-level>, line 1:\n"
         ".a.b)    \n"
         "jq: 1 compile error\n"},
        {"if . then 1",
         "jq: error: syntax error, unexpected end of file "
         "(Unix shell quoting issues?) at <top-level>, line 1:\n"
         "if . then 1          \n"
         "jq: error: Possibly unterminated 'if' statement at <top-level>, "
         "line 1:\n"
         "if . then 1\n"
         "jq: 2 compile errors\n"},
        {"if . then 1 2 end",
         "jq: error: syntax error, unexpected LITERAL "
         "(Unix shell quoting issues?) at <top-level>, line 1:\n"
         "if . then 1 2 end            \n"
         "jq: error: Possibly unterminated 'if' statement at <top-level>, "
         "line 1:\n"
         "if . then 1 2 end\n"
         "jq: 2 compile errors\n"},
        // An open bracket blocks the unterminated-if note.
        {"if . then (1",
         "jq: error: syntax error, unexpected end of file "
         "(Unix shell quoting issues?) at <top-level>, line 1:\n"
         "if . then (1           \n"
         "jq: 1 compile error\n"},
        {"try 1 catch",
         "jq: error: syntax error, unexpected end of file "
         "(Unix shell quoting issues?) at <top-level>, line 1:\n"
         "try 1 catch      \n"
         "jq: error: Possibly unterminated 'try' statement at <top-level>, "
         "line 1:\n"
         "try 1 catch\n"
         "jq: 2 compile errors\n"},
        {"try 1 catch 2 3",
         "jq: error: syntax error, unexpected LITERAL, expecting end of "
         "file (Unix shell quoting issues?) at <top-level>, line 1:\n"
         "try 1 catch 2 3              \n"
         "jq: 1 compile error\n"},
        {"1 . 2",
         "jq: error: syntax error, unexpected LITERAL, expecting FORMAT or "
         "QQSTRING_START or '[' (Unix shell quoting issues?) at <top-level>, "
         "line 1:\n"
         "1 . 2    \n"
         "jq: 1 compile error\n"},
        {"1 as",
         "jq: error: syntax error, unexpected end of file, expecting BINDING "
         "or '[' or '{' (Unix shell quoting issues?) at <top-level>, "
         "line 1:\n"
         "1 as  \n"
         "jq: 1 compile error\n"},
        {"1 as $x",
         "jq: error: syntax error, unexpected end of file, expecting '|' "
         "(Unix shell quoting issues?) at <top-level>, line 1:\n"
         "1 as $x     \n"
         "jq: 1 compile error\n"},
        {". as [$a $b] | 1",
         "jq: error: syntax error, unexpected BINDING, expecting ',' or "
         "']' (Unix shell quoting issues?) at <top-level>, line 1:\n"
         ". as [$a $b] | 1         \n"
         "jq: 1 compile error\n"},
        {". as {a} | 1",
         "jq: error: syntax error, unexpected '}', expecting ':' "
         "(Unix shell quoting issues?) at <top-level>, line 1:\n"
         ". as {a} | 1       \n"
         "jq: 1 compile error\n"},
        {"reduce . as $x",
         "jq: error: syntax error, unexpected end of file, expecting '(' "
         "(Unix shell quoting issues?) at <top-level>, line 1:\n"
         "reduce . as $x            \n"
         "jq: 1 compile error\n"},
        {"def f",
         "jq: error: syntax error, unexpected end of file, expecting '(' or "
         "':' (Unix shell quoting issues?) at <top-level>, line 1:\n"
         "def f    \n"
         "jq: 1 compile error\n"},
        {"{a:1",
         "jq: error: syntax error, unexpected end of file, expecting '}' "
         "(Unix shell quoting issues?) at <top-level>, line 1:\n"
         "{a:1   \n"
         "jq: 1 compile error\n"},
        {"{a: 1 | . + 1}",
         "jq: error: syntax error, unexpected '+', expecting '}' "
         "(Unix shell quoting issues?) at <top-level>, line 1:\n"
         "{a: 1 | . + 1}          \n"
         "jq: 1 compile error\n"},
        {"{a: 1?}",
         "jq: error: syntax error, unexpected '?', expecting '}' "
         "(Unix shell quoting issues?) at <top-level>, line 1:\n"
         "{a: 1?}     \n"
         "jq: 1 compile error\n"},
        {"{a: if . then 1 else 2 end}",
         "jq: error: syntax error, unexpected if (Unix shell quoting "
         "issues?) at <top-level>, line 1:\n"
         "{a: if . then 1 else 2 end}    \n"
         "jq: 1 compile error\n"},
        {"{,}",
         "jq: error: syntax error, unexpected ',' (Unix shell quoting "
         "issues?) at <top-level>, line 1:\n"
         "{,} \n"
         "jq: 1 compile error\n"},
        {"[1,]",
         "jq: error: syntax error, unexpected ']' (Unix shell quoting "
         "issues?) at <top-level>, line 1:\n"
         "[1,]   \n"
         "jq: 1 compile error\n"},
        {"\"abc",
         "jq: error: syntax error, unexpected end of file, expecting "
         "QQSTRING_TEXT or QQSTRING_INTERP_START or QQSTRING_END "
         "(Unix shell quoting issues?) at <top-level>, line 1:\n"
         "\"abc \n"
         "jq: 1 compile error\n"},
        {"1 == 1 == 1",
         "jq: error: syntax error, unexpected == (Unix shell quoting "
         "issues?) at <top-level>, line 1:\n"
         "1 == 1 == 1       \n"
         "jq: 1 compile error\n"},
        {".. ..",
         "jq: error: syntax error, unexpected .., expecting end of file "
         "(Unix shell quoting issues?) at <top-level>, line 1:\n"
         ".. ..   \n"
         "jq: 1 compile error\n"},
        {"1 $x",
         "jq: error: syntax error, unexpected BINDING, expecting end of "
         "file (Unix shell quoting issues?) at <top-level>, line 1:\n"
         "1 $x  \n"
         "jq: 1 compile error\n"},
        {"..a",
         "jq: error: syntax error, unexpected IDENT, expecting end of file "
         "(Unix shell quoting issues?) at <top-level>, line 1:\n"
         "..a  \n"
         "jq: 1 compile error\n"},
        {"\"\\(1 2)\"",
         "jq: error: syntax error, unexpected LITERAL (Unix shell quoting "
         "issues?) at <top-level>, line 1:\n"
         "\"\\(1 2)\"     \n"
         "jq: 1 compile error\n"},
        {".a\n| .b )",
         "jq: error: syntax error, unexpected INVALID_CHARACTER, expecting "
         "end of file (Unix shell quoting issues?) at <top-level>, "
         "line 2:\n"
         "| .b )     \n"
         "jq: 1 compile error\n"},
        // The FORMAT-key note joins only when the token after is ':'.
        {"{@base64: 2}",
         "jq: error: syntax error, unexpected ':', expecting QQSTRING_START "
         "(Unix shell quoting issues?) at <top-level>, line 1:\n"
         "{@base64: 2}        \n"
         "jq: error: May need parentheses around object key expression at "
         "<top-level>, line 1:\n"
         "{@base64: 2} \n"
         "jq: 2 compile errors\n"},
        // An escape error is reported where it sits in the program.
        {"\"\\q\"",
         "jq: error: Invalid escape at line 1, column 4 (while parsing "
         "'\"\\q\"') at <top-level>, line 1:\n"
         "\"\\q\" \n"
         "jq: 1 compile error\n"},
        {"\"ab\\q\"",
         "jq: error: Invalid escape at line 1, column 4 (while parsing "
         "'\"\\q\"') at <top-level>, line 1:\n"
         "\"ab\\q\"   \n"
         "jq: 1 compile error\n"},
        // Modules are not implemented: import is a syntax error anywhere.
        {"1 | import \"a\" as a; .",
         "jq: error: syntax error, unexpected import (Unix shell quoting "
         "issues?) at <top-level>, line 1:\n"
         "1 | import \"a\" as a; .    \n"
         "jq: 1 compile error\n"},
        {"def f: 1;",
         "jq: error: Top-level program not given (try \".\")\n"
         "jq: 1 compile error\n"},
        {"",
         "jq: error: Top-level program not given (try \".\")\n"
         "jq: 1 compile error\n"},
        {" # c",
         "jq: error: Top-level program not given (try \".\")\n"
         "jq: 1 compile error\n"},
    };
    for (const auto& one : cases)
        EXPECT_EQ(Errors(one.program), one.expected) << one.program;
}

TEST(JqParserTest, DeepNestingIsRefused) {
    // Past 256 levels of brackets the 257th is refused (a documented
    // exception: jq has no limit), worded as any other syntax error.
    std::string deep(300, '[');
    deep += "1";
    deep.append(300, ']');
    auto result = ParseProgram(deep);
    ASSERT_EQ(result.errors.size(), 1u);
    EXPECT_EQ(result.errors[0].offset, 256u);
    EXPECT_EQ(result.errors[0].message,
              "syntax error, unexpected '[' (Unix shell quoting issues?)");
    EXPECT_FALSE(result.root);

    // Call arguments, string interpolations and negations in an object
    // value nest too, and are refused the same way.
    std::string calls;
    for (int i = 0; i < 300; ++i) calls += "f(";
    calls += "1";
    calls.append(300, ')');
    result = ParseProgram(calls);
    ASSERT_EQ(result.errors.size(), 1u);
    EXPECT_EQ(result.errors[0].message,
              "syntax error, unexpected '(' (Unix shell quoting issues?)");
    std::string strings;
    for (int i = 0; i < 300; ++i) strings += "\"\\(";
    strings += "1";
    for (int i = 0; i < 300; ++i) strings += ")\"";
    result = ParseProgram(strings);
    ASSERT_EQ(result.errors.size(), 1u);
    EXPECT_EQ(result.errors[0].message,
              "syntax error, unexpected QQSTRING_INTERP_START (Unix shell "
              "quoting issues?)");
    std::string negations = "{a: ";
    for (int i = 0; i < 300; ++i) negations += "- ";
    negations += "1}";
    result = ParseProgram(negations);
    ASSERT_EQ(result.errors.size(), 1u);
    EXPECT_EQ(result.errors[0].message,
              "syntax error, unexpected '-' (Unix shell quoting issues?)");

    // Below the limit everything parses.
    std::string ok(200, '(');
    ok += "1";
    ok.append(200, ')');
    result = ParseProgram(ok);
    EXPECT_TRUE(result.errors.empty());
    ASSERT_TRUE(result.root);
    EXPECT_EQ(DumpNode(*result.root), "1");
}