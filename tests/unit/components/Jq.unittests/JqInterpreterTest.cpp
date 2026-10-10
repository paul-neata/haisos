#include <gtest/gtest.h>
#include <string>

#include "JqTestRunner.h"
#include "commands/jq/JqPrelude.h"

namespace {

using Haisos::Jq::RunJq;

std::string Repeat(const std::string& piece, int count) {
    std::string out;
    for (int i = 0; i < count; ++i)
        out += piece;
    return out;
}

TEST(JqInterpreterTest, PathsAndIteration) {
    EXPECT_EQ(RunJq(".a.b", "{\"a\":{\"b\":1}}"), "1");
    EXPECT_EQ(RunJq(".[1:]", "[1,2,3]"), "[2,3]");
    EXPECT_EQ(RunJq(".[-1:]", "[1,2,3]"), "[3]");
    EXPECT_EQ(RunJq(".[5:]", "[1,2,3]"), "[]");
    EXPECT_EQ(RunJq("\"abcdef\" | .[2:4]"), "\"cd\"");
    EXPECT_EQ(RunJq(".[]?", "5"), "");
    EXPECT_EQ(RunJq("null | .[1:2]"), "null");
    // .. : the input, then every value below it, pre-order.
    EXPECT_EQ(RunJq("..", "{\"a\":[1,{\"b\":2}]}"),
              "{\"a\":[1,{\"b\":2}]} [1,{\"b\":2}] 1 {\"b\":2} 2");
    // The key outer, the target inner.
    EXPECT_EQ(RunJq("[([1,2],[3,4])[0,1]]"), "[1,3,2,4]");
    // From outer, then to, then the target.
    EXPECT_EQ(RunJq("[([1,2,3],[4,5,6])[(0,1):(2,3)]]"),
              "[[1,2],[4,5],[1,2,3],[4,5,6],[2],[5],[2,3],[5,6]]");
    EXPECT_EQ(RunJq("[1,2,1] | .[[1]]"), "[0,2]");
    EXPECT_EQ(RunJq("[1,2,1,2] | .[[1,2]]"), "[0,2]");
    EXPECT_EQ(RunJq("[1,2] | .[[]]"), "[]");
    EXPECT_EQ(RunJq("[1,2,3] | .[1.7]"), "2");
    EXPECT_EQ(RunJq("[1,2,3] | .[-4]"), "null");
    EXPECT_EQ(RunJq("[1,2,3] | .[{\"start\":1,\"end\":null}]"), "[2,3]");
    EXPECT_EQ(RunJq("[1,2,3] | .[1.2:2.8]"), "[2,3]");
    EXPECT_EQ(RunJq("[1,2] | .[1:0]"), "[]");
    EXPECT_EQ(RunJq("[1,2] | .[-5:-4]"), "[]");
    // A NaN bound is its side's null: 0 from the start, the length to.
    // (The NaN comes from the input: nan/infinite are builtins of a later
    // task.)
    EXPECT_EQ(RunJq(". as $n | [1,2] | .[$n:2]", "nan"), "[1,2]");
    EXPECT_EQ(RunJq(". as $n | [1,2,3] | .[1:$n]", "nan"), "[2,3]");
    // Strings slice by code points.
    EXPECT_EQ(RunJq("\"aéb\" | .[1:2]"), "\"é\"");
}

TEST(JqInterpreterTest, IndexErrors) {
    EXPECT_EQ(RunJq(".[true]"), "error: Cannot index null with boolean");
    EXPECT_EQ(RunJq(".[null]"), "error: Cannot index null with null");
    EXPECT_EQ(RunJq(".[[1]]"), "error: Cannot index null with array");
    EXPECT_EQ(RunJq("{\"a\":1} | .[0]"),
              "error: Cannot index object with number");
    EXPECT_EQ(RunJq("{\"a\":1} | .[null]"),
              "error: Cannot index object with null");
    EXPECT_EQ(RunJq("[1,2] | .[1:\"x\"]"),
              "error: Array/string slice indices must be integers");
    EXPECT_EQ(RunJq("{\"a\":1} | .[1:2]"),
              "error: Cannot index object with object");
    EXPECT_EQ(RunJq("1 | .[1:2]"),
              "error: Cannot index number with object");
    // A string key shorter than 30 bytes is named; a longer one is not.
    const std::string key29(29, 'k');
    const std::string key30(30, 'k');
    EXPECT_EQ(RunJq("1 | .[\"" + key29 + "\"]"),
              "error: Cannot index number with string \"" + key29 + "\"");
    EXPECT_EQ(RunJq("1 | .[\"" + key30 + "\"]"),
              "error: Cannot index number with string");
    EXPECT_EQ(RunJq("\"abc\" | .[0]"),
              "error: Cannot index string with number");
    EXPECT_EQ(RunJq("\"abc\" | .[null]"),
              "error: Cannot index string with null");
    EXPECT_EQ(RunJq("true | .[0]"),
              "error: Cannot index boolean with number");
    EXPECT_EQ(RunJq("1 | .[]"), "error: Cannot iterate over number (1)");
    EXPECT_EQ(RunJq("null | .[]"), "error: Cannot iterate over null (null)");
    EXPECT_EQ(RunJq("\"abcdefghij\" | .[]"),
              "error: Cannot iterate over string (\"abcdefghij\")");
}

TEST(JqInterpreterTest, Arithmetic) {
    EXPECT_EQ(RunJq("null + 3.0"), "3.0");
    EXPECT_EQ(RunJq("{\"a\":1,\"b\":2} + {\"c\":0,\"a\":3}"),
              "{\"a\":3,\"b\":2,\"c\":0}");
    EXPECT_EQ(RunJq("[1,2,3,1] - [1]"), "[2,3]");
    EXPECT_EQ(RunJq("[1,[2]] - [[2]]"), "[1]");
    EXPECT_EQ(RunJq("\"ab\" * 0"), "\"\"");
    EXPECT_EQ(RunJq("\"ab\" * 0.5"), "\"\"");
    EXPECT_EQ(RunJq("\"ab\" * 1.5"), "\"ab\"");
    EXPECT_EQ(RunJq("\"ab\" * 2.5"), "\"abab\"");
    EXPECT_EQ(RunJq("\"ab\" * -1"), "null");
    EXPECT_EQ(RunJq("2 * \"ab\""), "\"abab\"");
    EXPECT_EQ(RunJq("{\"a\":{\"b\":1}} * {\"a\":{\"c\":2}}"),
              "{\"a\":{\"b\":1,\"c\":2}}");
    EXPECT_EQ(RunJq("{\"a\":{\"b\":1}} * {\"a\":2}"), "{\"a\":2}");
    EXPECT_EQ(RunJq("\"a,b\" / \",\""), "[\"a\",\"b\"]");
    EXPECT_EQ(RunJq("\"abc\" / \"\""), "[\"a\",\"b\",\"c\"]");
    // Arithmetic results are computed numbers: the literal spelling goes.
    EXPECT_EQ(RunJq("1.0 + 0"), "1");
    EXPECT_EQ(RunJq("1 < \"a\""), "true");
    EXPECT_EQ(RunJq("[5 % -2, -5 % 3, 5.9 % 2.1, 1e30 % 7, -4 % 2, -5 % -3, "
                    "1e19 % 10, -1e19 % 10, -0 % 3, 0 % -3]"),
              "[1,-2,1,0,-0,-2,7,-7,-0,0]");
    EXPECT_EQ(RunJq("[. % 3, 5 % .]", "nan"), "[null,null]");
    EXPECT_EQ(RunJq("[1e1000 % 7, 7 % 1e1000, -1e1000 % 7]"), "[0,7,-0]");
    EXPECT_EQ(RunJq("\"abcdefghijklmnopq\" - 1"),
              "error: string (\"abcdefghij...) and number (1) cannot be "
              "subtracted");
    EXPECT_EQ(RunJq("null - null"),
              "error: null (null) and null (null) cannot be subtracted");
    EXPECT_EQ(RunJq("{} * 2"),
              "error: object ({}) and number (2) cannot be multiplied");
    EXPECT_EQ(RunJq("[] - 1"),
              "error: array ([]) and number (1) cannot be subtracted");
    EXPECT_EQ(RunJq("\"a\" * \"b\""),
              "error: string (\"a\") and string (\"b\") cannot be multiplied");
    EXPECT_EQ(RunJq("[1] * 2"),
              "error: array ([1]) and number (2) cannot be multiplied");
    EXPECT_EQ(RunJq("\"a\" / 0"),
              "error: string (\"a\") and number (0) cannot be divided");
    EXPECT_EQ(RunJq("1 / 0"),
              "error: number (1) and number (0) cannot be divided because "
              "the divisor is zero");
    EXPECT_EQ(RunJq("5 % 0.5"),
              "error: number (5) and number (0.5) cannot be divided "
              "(remainder) because the divisor is zero");
    EXPECT_EQ(RunJq("[1,2,3,4,5,6,7,8,9] | -."),
              "error: array ([1,2,3,4,5,...) cannot be negated");
}

TEST(JqInterpreterTest, Generators) {
    // The right operand of a binary operator is the outer one.
    EXPECT_EQ(RunJq("[(1,2) + (10,20)]"), "[11,12,21,22]");
    // The last string interpolation is the outer one.
    EXPECT_EQ(RunJq("\"\\(1,2)-\\(3,4)\""),
              "\"1-3\" \"2-3\" \"1-4\" \"2-4\"");
    // Object entries left to right, the key outer within an entry.
    EXPECT_EQ(RunJq("[{a:(1,2), b:(3,4)}]"),
              "[{\"a\":1,\"b\":3},{\"a\":1,\"b\":4},{\"a\":2,\"b\":3},"
              "{\"a\":2,\"b\":4}]");
    EXPECT_EQ(RunJq("{(\"a\",\"b\"): (1,2)}"),
              "{\"a\":1} {\"a\":2} {\"b\":1} {\"b\":2}");
    EXPECT_EQ(RunJq("[(true,false) and (true,false)]"), "[true,false,false]");
    EXPECT_EQ(RunJq("[(true,false) or (true,false)]"), "[true,true,false]");
    EXPECT_EQ(RunJq("[(null, false) // (3, 4)]"), "[3,4]");
    EXPECT_EQ(RunJq("[false, null] | .[] // 4"), "4");
    EXPECT_EQ(RunJq("[.a.b // 3]"), "[3]");
    // An error inside the left side of // propagates.
    EXPECT_EQ(RunJq("[(1, error(\"x\"), 2) // 3]"), "error: x");
    EXPECT_EQ(RunJq("[error(\"x\") // 3]"), "error: x");
    EXPECT_EQ(RunJq("[(1 | .a) // 3]"),
              "error: Cannot index number with string \"a\"");
    // The formats: @text is tostring, @json the compact JSON; the value
    // interpolated decides, a lone @foo "..." its text alone.
    EXPECT_EQ(RunJq("\"\\(1,2)\" | @text \"x\\(.)\""), "\"x1\" \"x2\"");
    EXPECT_EQ(RunJq("\"\\(1,2)\" | @text \"x\\(.)\", @json \"y\\(\"q\")\""),
              "\"x1\" \"y\\\"q\\\"\" \"x2\" \"y\\\"q\\\"\"");
    EXPECT_EQ(RunJq("\"a\" | @foo"), "error: foo is not a valid format");
    EXPECT_EQ(RunJq("@foo \"a\""), "\"a\"");
}

TEST(JqInterpreterTest, TryCatch) {
    EXPECT_EQ(RunJq("try error(\"x\") catch ."), "\"x\"");
    EXPECT_EQ(RunJq("try error({}) catch ."), "{}");
    EXPECT_EQ(RunJq("try error(null) catch ."), "null");
    EXPECT_EQ(RunJq("[.[] | try if . == 2 then error(\"e\") else . end "
                    "catch \"c\"]", "[1,2,3]"),
              "[1,\"c\",3]");
    // The body is not resumed after its error.
    EXPECT_EQ(RunJq("try (1, error(\"x\"), 3) catch ."), "1 \"x\"");
    EXPECT_EQ(RunJq("1 | .a?"), "");
    // An error raised downstream is not caught by the try whose body
    // produced the value that raised it.
    EXPECT_EQ(RunJq("try (1,2) catch . | error(\"down\")"), "error: down");
    EXPECT_EQ(RunJq("try ((try (1,2) catch \"inner\") | error(\"down\")) "
                    "catch \"outer: \\(.)\""),
              "\"outer: down\"");
}

TEST(JqInterpreterTest, ReduceForeach) {
    // Each init output starts its own reduction.
    EXPECT_EQ(RunJq("[reduce empty as $x (1,2; .)]"), "[1,2]");
    // The state is the update's last output, null when it gave none.
    EXPECT_EQ(RunJq("[reduce (1,2) as $x (0; empty)]"), "[null]");
    EXPECT_EQ(RunJq("[foreach (3,4) as $x (0,10; . + $x)]"), "[3,7,13,17]");
    EXPECT_EQ(RunJq("[foreach (1,2) as $x (0; . + $x; [$x, .])]"),
              "[[1,1],[2,3]]");
    EXPECT_EQ(RunJq("[foreach (1,2,3) as $x (0; if $x == 2 then empty "
                    "else . + $x end; .)]"),
              "[1,3]");
    EXPECT_EQ(RunJq("[foreach (1,2) as $x (0; (. + $x), 7)]"), "[1,7,9,7]");
    // The ?// alternatives retry the item, in destructuring or the update.
    EXPECT_EQ(RunJq("reduce ([1],2) as [$a] ?// $a (0; . + $a)"), "3");
    EXPECT_EQ(RunJq("reduce ([1]) as [$a] ?// $a (0; if $a == 1 then "
                    "error(\"x\") else 5 end)"),
              "5");
}

TEST(JqInterpreterTest, Destructuring) {
    EXPECT_EQ(RunJq(". as [$a, {b: $c, $d}] | [$a,$c,$d]",
                    "[1,{\"b\":2,\"d\":3}]"),
              "[1,2,3]");
    // Every alternative binds every variable, null unless it set it.
    EXPECT_EQ(RunJq("{\"a\":1} as {$a, b: $b} ?// [$c] | [$a, $b, $c]"),
              "[1,null,null]");
    EXPECT_EQ(RunJq("\"x\" as [$a] ?// $b | [$a,$b]"), "[null,\"x\"]");
    EXPECT_EQ(RunJq("[[1,2]] | [.[] as [$a] ?// $a | if $a == 1 then "
                    "error(\"x\") else $a end]"),
              "[[1,2]]");
    // Keywords and null are plain keys; a key expression is evaluated on
    // the value being destructured, each output one binding set.
    EXPECT_EQ(RunJq(". as {b: $c, null: $a, true: $t} | [$c,$a,$t]",
                    "{\"b\":5,\"null\":6}"),
              "[5,6,null]");
    EXPECT_EQ(RunJq(". as {(\"a\",\"b\"): $x} | $x"), "null null");
    EXPECT_EQ(RunJq("{\"k\":{\"a\":\"b\",\"b\":1}} | .k as {(.a): $v} | $v"),
              "1");
    EXPECT_EQ(RunJq("1 as [$a] | $a"),
              "error: Cannot index number with number");
    EXPECT_EQ(RunJq("{} as $x | {($x):1}"),
              "error: Cannot use object ({}) as object key");
}

TEST(JqInterpreterTest, Functions) {
    EXPECT_EQ(RunJq("def f(g): [g, g]; f(1, 2)"), "[1,2,1,2]");
    EXPECT_EQ(RunJq("def f($x; y): $x + y; f(1; 2)"), "3");
    // The first $value parameter is the outer one.
    EXPECT_EQ(RunJq("def f($a; $b): [$a, $b]; [f(1,2; 3,4)]"),
              "[[1,3],[1,4],[2,3],[2,4]]");
    EXPECT_EQ(RunJq("def f(x): x as $v | $v; [f(1,2)]"), "[1,2]");
    EXPECT_EQ(RunJq("def f: def g: 3; g; f"), "3");
    EXPECT_EQ(RunJq("def fac: if . <= 1 then 1 else . * (. - 1 | fac) end; "
                    "10 | fac"),
              "3628800");
    // A closure sees its definition scope, not its caller's.
    EXPECT_EQ(RunJq("1 as $x | def f: $x; 2 as $x | f"), "1");
    // A definition shadows the prelude and the natives.
    EXPECT_EQ(RunJq("def select(f): 5; 1 | select(.)"), "5");
    EXPECT_EQ(RunJq("def empty: 3; [empty]"), "[3]");
    EXPECT_EQ(RunJq("def type: 1; type"), "1");
    // The prelude.
    EXPECT_EQ(RunJq("[1 | select(true, false, true)]"), "[1,1]");
    EXPECT_EQ(RunJq("[0 | recurse(if . < 3 then .+1 else empty end)]"),
              "[0,1,2,3]");
    EXPECT_EQ(RunJq("[[1,[2]] | recurse(.[]?)]"), "[[1,[2]],1,[2],2]");
    // The core natives.
    EXPECT_EQ(RunJq("[.[] | not]", "[1,2]"), "[false,false]");
    EXPECT_EQ(RunJq("type"), "\"null\"");
}

TEST(JqInterpreterTest, LabelsAndLoc) {
    EXPECT_EQ(RunJq("[label $out | 1, 2, break $out, 3]"), "[1,2]");
    EXPECT_EQ(RunJq("[label $a | 1, (label $b | 2, break $a, 3), 4]"),
              "[1,2]");
    EXPECT_EQ(RunJq("$__loc__"),
              "{\"file\":\"<top-level>\",\"line\":1}");
    EXPECT_EQ(RunJq("1 as $x |\n$__loc__"),
              "{\"file\":\"<top-level>\",\"line\":2}");
    EXPECT_EQ(RunJq("{$__loc__}"),
              "{\"__loc__\":{\"file\":\"<top-level>\",\"line\":1}}");
}

TEST(JqInterpreterTest, LiteralsSurvive) {
    EXPECT_EQ(RunJq("{\"a\":1.0} | .a"), "1.0");
    EXPECT_EQ(RunJq("1.0 + 0"), "1");
    EXPECT_EQ(RunJq("null + 3.0"), "3.0");
    EXPECT_EQ(RunJq("[1.0, 100000000000000000001]"),
              "[1.0,100000000000000000001]");
}

TEST(JqInterpreterTest, CompileErrors) {
    // A program's compile errors, exactly as jq prints them: every location
    // block, then the count line. The padding after each line is the
    // error's offset in it.
    EXPECT_EQ(RunJq("foo"),
              "jq: error: foo/0 is not defined at <top-level>, line 1:\n"
              "foo\n"
              "jq: 1 compile error\n");
    EXPECT_EQ(RunJq("foo | bar"),
              "jq: error: foo/0 is not defined at <top-level>, line 1:\n"
              "foo | bar\n"
              "jq: error: bar/0 is not defined at <top-level>, line 1:\n"
              "foo | bar      \n"
              "jq: 2 compile errors\n");
    EXPECT_EQ(RunJq("def f: 1; f(2)"),
              "jq: error: f/1 is not defined at <top-level>, line 1:\n"
              "def f: 1; f(2)          \n"
              "jq: 1 compile error\n");
    EXPECT_EQ(RunJq("1 as $x | $y"),
              "jq: error: $y is not defined at <top-level>, line 1:\n"
              "1 as $x | $y          \n"
              "jq: 1 compile error\n");
    EXPECT_EQ(RunJq(".[$x]"),
              "jq: error: $x is not defined at <top-level>, line 1:\n"
              ".[$x]  \n"
              "jq: 1 compile error\n");
    EXPECT_EQ(RunJq("label $f | break $g"),
              "jq: error: $*label-g is not defined at <top-level>, line 1:\n"
              "label $f | break $g           \n"
              "jq: 1 compile error\n");
    EXPECT_EQ(RunJq("break $x"),
              "jq: error: $*label-x is not defined at <top-level>, line 1:\n"
              "break $x\n"
              "jq: 1 compile error\n");
    // A definition sees itself and the earlier ones, not the later ones.
    EXPECT_EQ(RunJq("def f: g; def g: 1; f"),
              "jq: error: g/0 is not defined at <top-level>, line 1:\n"
              "def f: g; def g: 1; f       \n"
              "jq: 1 compile error\n");
    // A reduce's pattern variables are not in scope for the init.
    EXPECT_EQ(RunJq("[reduce (1,2) as $x ($x; .)]"),
              "jq: error: $x is not defined at <top-level>, line 1:\n"
              "[reduce (1,2) as $x ($x; .)]                     \n"
              "jq: 1 compile error\n");
    // Constant object keys of the wrong kind, in a construction or a
    // pattern, at the key's parenthesis.
    EXPECT_EQ(RunJq("{(1):2}"),
              "jq: error: Cannot use number (1) as object key at "
              "<top-level>, line 1:\n"
              "{(1):2} \n"
              "jq: 1 compile error\n");
    EXPECT_EQ(RunJq("{(true):2, (null):3}"),
              "jq: error: Cannot use boolean (true) as object key at "
              "<top-level>, line 1:\n"
              "{(true):2, (null):3} \n"
              "jq: error: Cannot use null (null) as object key at "
              "<top-level>, line 1:\n"
              "{(true):2, (null):3}           \n"
              "jq: 2 compile errors\n");
    EXPECT_EQ(RunJq("[.[] as {(1): $x} | $x]"),
              "jq: error: Cannot use number (1) as object key at "
              "<top-level>, line 1:\n"
              "[.[] as {(1): $x} | $x]         \n"
              "jq: 1 compile error\n");
    EXPECT_EQ(RunJq("{(1.0):2}"),
              "jq: error: Cannot use number (1.0) as object key at "
              "<top-level>, line 1:\n"
              "{(1.0):2} \n"
              "jq: 1 compile error\n");
    EXPECT_EQ(RunJq("{(1e2):2}"),
              "jq: error: Cannot use number (1E+2) as object key at "
              "<top-level>, line 1:\n"
              "{(1e2):2} \n"
              "jq: 1 compile error\n");
    EXPECT_EQ(RunJq("{(100000000000000000001):2}"),
              "jq: error: Cannot use number (10000000000...) as object key "
              "at <top-level>, line 1:\n"
              "{(100000000000000000001):2} \n"
              "jq: 1 compile error\n");
    // Keys jq computes from constants fail at run time here (documented).
    EXPECT_EQ(RunJq("{(1+1):2}"),
              "error: Cannot use number (2) as object key");
    EXPECT_EQ(RunJq("{(-1):2}"),
              "error: Cannot use number (-1) as object key");
}

TEST(JqInterpreterTest, DepthLimit) {
    // Recursion about a hundred calls deep fits: each call costs a handful
    // of evaluation levels.
    EXPECT_EQ(RunJq("def f: if . < 100 then .+1|f else . end; 0|f"), "100");
    // Endless recursion stops at the bound instead of running out of
    // memory (a documented difference from jq).
    EXPECT_EQ(RunJq("def f: .+1|f; 0|f"),
              "error: Maximum evaluation depth (1024) exceeded");
    // A tall tree within the parser's bound evaluates.
    EXPECT_EQ(RunJq("[" + Repeat("1,", 400) + "1] | .[400]"), "1");
    // Chained bindings (each costs about two tree levels, so the tree
    // bound allows roughly 250 of them: the plan's 300 is more than the
    // 512-level tree height, and 200 sit well inside it).
    EXPECT_EQ(RunJq(Repeat(". as $x | ", 200) + "$x"), "null");
}

TEST(JqInterpreterTest, PreludeParses) {
    const auto& parsed = Haisos::Jq::ParsedPrelude();
    EXPECT_TRUE(parsed.errors.empty());
    ASSERT_TRUE(parsed.root != nullptr);
    EXPECT_EQ(parsed.root->type, Haisos::Jq::NodeType::Defs);
    ASSERT_EQ(parsed.root->definitions.size(), 2u);
    EXPECT_EQ(parsed.root->definitions[0].name, "select");
    EXPECT_EQ(parsed.root->definitions[1].name, "recurse");
}

} // namespace