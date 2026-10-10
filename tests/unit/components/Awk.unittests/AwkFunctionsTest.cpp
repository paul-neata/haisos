#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "AwkRunFixture.h"

namespace Haisos {
namespace {

// awk--functions: user-defined functions and the string built-ins. Every
// expectation is gawk --posix 5.2.1's own output.
class AwkFunctionsTest : public AwkRunTest {};

// --- user-defined functions ---

TEST_F(AwkFunctionsTest, UserFunctions) {
    // Parameters by value; `return' with a value.
    Captured captured = RunCaptured("awk",
        {R"(function f(a, b) { return a + b } BEGIN { print f(1, 2) })"});
    EXPECT_EQ(captured.out, "3\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Recursion, and numbers beyond the double exactly (fact(25)).
    captured = RunCaptured("awk",
        {R"(function fact(n) { return n <= 1 ? 1 : n * fact(n - 1) )"
         R"(} BEGIN { print fact(10), fact(20), fact(25) })"});
    EXPECT_EQ(captured.out, "3628800 2432902008176640000 15511210043330986055303168\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Called before its definition.
    captured = RunCaptured("awk",
        {R"(BEGIN { print g(2) } function g(x) { return x * x })"});
    EXPECT_EQ(captured.out, "4\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Extra parameters are locals; `return' without a value leaves the
    // variable uninitialized; a global assigned in the function is the
    // caller's.
    captured = RunCaptured("awk",
        {R"(function f(a,  i) { i++; t = i; return } )"
         R"(BEGIN { x = f(1); print "[" x "]", length(x); f(); print t })"});
    EXPECT_EQ(captured.out, "[] 0\n1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A scalar parameter is a copy: the caller's variable is not changed.
    captured = RunCaptured("awk",
        {R"(function f(s) { s = s "x"; return s } BEGIN { t = "a"; print f(t), t })"});
    EXPECT_EQ(captured.out, "ax a\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A global reached by name inside the body.
    captured = RunCaptured("awk",
        {R"(BEGIN { x = 5; f(); print x } function f() { x = 7 })"});
    EXPECT_EQ(captured.out, "7\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // An extra parameter shadows the global of the same name.
    captured = RunCaptured("awk",
        {R"(function f(x, y) { y = x * 2; return y } BEGIN { y = 5; print f(3), y })"});
    EXPECT_EQ(captured.out, "6 5\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Nested calls unwind in order.
    captured = RunCaptured("awk",
        {R"(function f(n) { if (n > 0) f(n - 1); print n } BEGIN { f(3) })"});
    EXPECT_EQ(captured.out, "0\n1\n2\n3\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Missing arguments are uninitialized, not an error.
    captured = RunCaptured("awk",
        {R"(function f(a, b) { print a "|" b "|" } BEGIN { f(1); f() })"});
    EXPECT_EQ(captured.out, "1||\n||\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // $0 set inside the body: the fields of the caller's moment.
    captured = RunCaptured("awk",
        {R"(function f(x) { $0 = x; return NF } BEGIN { print f("a b c") })"});
    EXPECT_EQ(captured.out, "3\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // The special variables, NF included, are passed by value.
    captured = RunCaptured("awk",
        {R"(function f(a) { a = 9; return a "|" NF } )"
         R"({ print f(NF), NF, f(NR), f(FS) "." })"}, "a b c\n");
    EXPECT_EQ(captured.out, "9|3 3 9|3 9|3.\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // 200 active calls are allowed; the 201st is refused (FunctionErrors).
    captured = RunCaptured("awk",
        {R"(function f(n) { return n == 0 ? 0 : 1 + f(n - 1) )"
         R"(} BEGIN { print f(199) })"});
    EXPECT_EQ(captured.out, "199\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A Scalar argument passed by name: its own copy, and no "(from ...)"
    // name built for it.
    captured = RunCaptured("awk",
        {R"(function f(a) { a = a + 1; return a } function g(b) { return f(b) } )"
         R"(BEGIN { x = 1; print g(x), x })"});
    EXPECT_EQ(captured.out, "2 1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkFunctionsTest, ArraysByReference) {
    // An array parameter is the caller's array itself.
    Captured captured = RunCaptured("awk",
        {R"(function f(a) { a[2] = 2; delete a[1] } )"
         R"(BEGIN { x[1] = 1; f(x); for (k in x) print k, x[k] })"});
    EXPECT_EQ(captured.out, "2 2\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // An untyped argument passed by name: the callee may make it an array
    // in the caller.
    captured = RunCaptured("awk",
        {R"(function f(a) { a[1] = 1 } BEGIN { f(x); print x[1] })"});
    EXPECT_EQ(captured.out, "1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // The binding reaches through a chain of untyped parameters.
    captured = RunCaptured("awk",
        {R"(function f(a) { a[1] = 1 } function g(  y) { f(y); print y[1] } )"
         R"(BEGIN { g() })"});
    EXPECT_EQ(captured.out, "1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk",
        {R"(function g(b) { b["z"] = 9 } function f(a) { g(a) } )"
         R"(BEGIN { f(arr); print arr["z"] })"});
    EXPECT_EQ(captured.out, "9\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // split fills the array it was given, the caller's here.
    captured = RunCaptured("awk",
        {R"(function f(a) { split("p q r", a) } BEGIN { f(arr); print arr[3] })"});
    EXPECT_EQ(captured.out, "r\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // delete over the caller's array, for-in emptying it.
    captured = RunCaptured("awk",
        {R"(function f(a, i) { for (i in a) delete a[i] } )"
         R"(BEGIN { x[1]; x[2]; f(x); for (k in x) n++; print n + 0 })"});
    EXPECT_EQ(captured.out, "0\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // An element passed as a value: the array gains the element read.
    captured = RunCaptured("awk",
        {R"(function f(x) { } BEGIN { f(a[1]); for (k in a) n++; print n + 0 })"});
    EXPECT_EQ(captured.out, "1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A call alone does not make the caller's variable an array.
    captured = RunCaptured("awk",
        {R"(function f(a) { } BEGIN { f(x); x[1] = 2; print x[1] })"});
    EXPECT_EQ(captured.out, "2\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk",
        {R"(function f(a) { a = 1 } BEGIN { f(x); print "x=" x })"});
    EXPECT_EQ(captured.out, "x=\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

// --- flow out of a function ---

TEST_F(AwkFunctionsTest, FlowInsideFunctions) {
    // next in a function acts on the calling rule.
    Captured captured = RunCaptured("awk",
        {R"(function f() { next } { f(); print "no" } END { print NR })", "/abc.txt"});
    EXPECT_EQ(captured.out, "2\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Even out of a pattern.
    captured = RunCaptured("awk",
        {R"(function f() { next } f() { print "no" } END { print "end", NR })"}, "a b c\n");
    EXPECT_EQ(captured.out, "end 1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // exit in a function exits, with its code.
    captured = RunCaptured("awk",
        {R"(function f() { exit 4 } BEGIN { f(); print "no" })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 4);

    // nextfile in a function acts on the calling rule.
    captured = RunCaptured("awk",
        {R"(function f() { nextfile } FNR == 1 { print FILENAME; f() })",
         "/abc.txt", "/data.csv"});
    EXPECT_EQ(captured.out, "/abc.txt\n/data.csv\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

// --- the function errors ---

TEST_F(AwkFunctionsTest, FunctionErrors) {
    // An undefined function is a fatal at the call.
    Captured captured = RunCaptured("awk", {R"(BEGIN { print "x"; foo() })"});
    EXPECT_EQ(captured.out, "x\n");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: function `foo' not defined\n");
    EXPECT_EQ(captured.status, 2);

    // Never reached, never reported.
    captured = RunCaptured("awk", {R"(BEGIN { if (0) foo(); print "ok" })"});
    EXPECT_EQ(captured.out, "ok\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A scalar argument to an array parameter.
    captured = RunCaptured("awk",
        {R"(function f(a) { a[1] = 1 } BEGIN { x = 1; f(x) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: attempt to use scalar parameter "
                           "`a' as an array\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk",
        {R"(function f(a) { a[1] = 1 } BEGIN { f(1) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: attempt to use scalar parameter "
                           "`a' as an array\n");
    EXPECT_EQ(captured.status, 2);

    // An array argument to a scalar parameter, the error naming where the
    // argument came from.
    captured = RunCaptured("awk",
        {R"(function f(a) { a = 1 } BEGIN { x[1] = 1; f(x) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: attempt to use array "
                           "`a (from x)' in a scalar context\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk",
        {R"(function f(a) { return a } BEGIN { x[1]; print f(x) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: attempt to use array "
                           "`a (from x)' in a scalar context\n");
    EXPECT_EQ(captured.status, 2);

    // The typing follows the binding: a scalar use in the callee makes the
    // caller's variable a scalar, and its later array use fails.
    captured = RunCaptured("awk",
        {R"(function f(a) { print "[" a "]" } BEGIN { f(x); x[1] = 2 })"});
    EXPECT_EQ(captured.out, "[]\n");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: attempt to use scalar "
                           "`x' as an array\n");
    EXPECT_EQ(captured.status, 2);

    // Through a chain: a scalar use deeper in makes the parameter a scalar.
    captured = RunCaptured("awk",
        {R"(function g(b) { b = 1 } function f(a) { g(a); a[1] = 2 } )"
         R"(BEGIN { f(arr) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: attempt to use scalar parameter "
                           "`a' as an array\n");
    EXPECT_EQ(captured.status, 2);

    // Through a chain: the Array parameter itself, the "(from ...)" names of
    // the whole chain joined.
    captured = RunCaptured("awk",
        {R"(function f(a) { return a } function g(b) { return f(b) } )"
         R"(BEGIN { y[1]; g(y) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: attempt to use array "
                           "`a (from b, from y)' in a scalar context\n");
    EXPECT_EQ(captured.status, 2);

    // next/nextfile out of BEGIN or END.
    captured = RunCaptured("awk", {R"(function f() { next } BEGIN { f() })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: `next' cannot be called from a "
                            "`BEGIN' rule\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {R"(function f() { nextfile } BEGIN { f() })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: `nextfile' cannot be called from "
                            "a `BEGIN' rule\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {R"(function f() { next } END { f() })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: `next' cannot be called from a "
                            "`END' rule\n");
    EXPECT_EQ(captured.status, 2);

    // Too many arguments is a warning, the extras evaluated anyway.
    captured = RunCaptured("awk",
        {R"(function f(a) { return a } BEGIN { print f(1, y = 5); print y })"});
    EXPECT_EQ(captured.out, "1\n5\n");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: warning: function `f' called with more "
                            "arguments than declared\n");
    EXPECT_EQ(captured.status, 0);

    // The warning is per call.
    captured = RunCaptured("awk",
        {R"(function f(a) { } BEGIN { for (i = 0; i < 2; i++) f(1, 2); print "ok" })"});
    EXPECT_EQ(captured.out, "ok\n");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: warning: function `f' called with more "
                            "arguments than declared\n"
                            "awk: cmd. line:1: warning: function `f' called with more "
                            "arguments than declared\n");
    EXPECT_EQ(captured.status, 0);

    // The depth limit: 200 active calls are allowed, the 201st is fatal
    // (gawk has no fixed limit).
    captured = RunCaptured("awk",
        {R"(function f(n) { return n == 0 ? 0 : 1 + f(n - 1) )"
         R"(} BEGIN { print f(200) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: function call nesting too deep "
                            "(more than 200 calls)\n");
    EXPECT_EQ(captured.status, 2);
}

// The #82 follow-ups: a binding chain whose end turns out to be an array,
// read as a scalar after the call began -- gawk's fatal, the name listing
// the whole chain.
TEST_F(AwkFunctionsTest, LateArrayBinding) {
    // The caller's variable became an array after the call began.
    Captured captured = RunCaptured("awk",
        {R"(function f(a) { x[1] = 1; print "[" a "]" } BEGIN { f(x) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: attempt to use array "
                           "`a (from x)' in a scalar context\n");
    EXPECT_EQ(captured.status, 2);

    // The same for an assignment, an increment and a condition.
    captured = RunCaptured("awk",
        {R"(function f(a) { x[1] = 1; a = 3 } BEGIN { f(x) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: attempt to use array "
                           "`a (from x)' in a scalar context\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk",
        {R"(function f(a) { x[1] = 1; a++ } BEGIN { f(x) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: attempt to use array "
                           "`a (from x)' in a scalar context\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk",
        {R"(function f(a) { x[1] = 1; if (a) print "t" } BEGIN { f(x) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: attempt to use array "
                           "`a (from x)' in a scalar context\n");
    EXPECT_EQ(captured.status, 2);

    // The chain names every parameter it went through.
    captured = RunCaptured("awk",
        {R"(function g(b) { print b } function f(a) { g(a) } )"
         R"(BEGIN { x[1] = 1; f(x) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: attempt to use array "
                           "`b (from a, from x)' in a scalar context\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk",
        {R"(function h(c) { x[1] = 1; print c } function g(b) { h(b) } )"
         R"(function f(a) { g(a) } BEGIN { f(x) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: attempt to use array "
                           "`c (from b, from a, from x)' in a scalar context\n");
    EXPECT_EQ(captured.status, 2);

    // A local (an extra parameter) is named as the one it was passed from.
    captured = RunCaptured("awk",
        {R"(function g(b) { print b } function f(a, l) { l[1] = 1; g(l) } )"
         R"(BEGIN { f(1) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: attempt to use array "
                           "`b (from l)' in a scalar context\n");
    EXPECT_EQ(captured.status, 2);

    // The scalar side keeps the plain name.
    captured = RunCaptured("awk",
        {R"(function g(b) { b[1] = 1 } function f(a) { x = 1; g(a) } )"
         R"(BEGIN { f(x) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: attempt to use scalar "
                           "parameter `b' as an array\n");
    EXPECT_EQ(captured.status, 2);

    // length of a parameter whose chain turns out to be an array.
    captured = RunCaptured("awk",
        {R"(function f(a) { x[1] = 1; print length(a) } BEGIN { f(x) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: length: received array "
                            "argument\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk",
        {R"(function g(b) { x[1] = 1; print length(b) } function f(a) { g(a) } )"
         R"(BEGIN { f(x) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: length: received array "
                            "argument\n");
    EXPECT_EQ(captured.status, 2);

    // A chain that ends in a scalar: length of it is 0, and no error.
    captured = RunCaptured("awk",
        {R"(function f(a) { x = 5; print length(a) } BEGIN { f(x) })"});
    EXPECT_EQ(captured.out, "0\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // split into a parameter whose chain turns out to be a scalar.
    captured = RunCaptured("awk",
        {R"(function f(a) { x = 1; split("a b", a) } BEGIN { f(x) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: split: second argument is "
                            "not an array\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk",
        {R"(function g(b) { x = 1; split("a", b) } function f(a) { g(a) } )"
         R"(BEGIN { f(x) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: split: second argument is "
                            "not an array\n");
    EXPECT_EQ(captured.status, 2);
}

// --- length, substr, index, tolower, toupper ---

TEST_F(AwkFunctionsTest, LengthSubstrIndexCase) {
    Captured captured = RunCaptured("awk",
        {R"(BEGIN { print length("abc"), length(12345), length(1/3), length() })"});
    EXPECT_EQ(captured.out, "3 5 8 0\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A bare length (no parentheses) is length($0), and concatenates.
    captured = RunCaptured("awk",
        {R"({ print length, length(), length $1, length($2) })"}, "hello world\n");
    EXPECT_EQ(captured.out, "11 11 11hello 5\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // An array argument to length, global or parameter.
    captured = RunCaptured("awk", {R"(BEGIN { x[1]; print length(x) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: length: received array argument\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk",
        {R"(function f(a) { return length(a) } BEGIN { x[1]; print f(x) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: length: received array argument\n");
    EXPECT_EQ(captured.status, 2);

    // length types its argument a scalar, an array use after it fails.
    captured = RunCaptured("awk", {R"(BEGIN { print length(x); x[1] = 1 })"});
    EXPECT_EQ(captured.out, "0\n");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: attempt to use scalar `x' as an array\n");
    EXPECT_EQ(captured.status, 2);

    // substr's start and length are truncated toward zero; a start below 1
    // is 1 without shortening the length; a length of none (NaN, 0,
    // negative) is none at all; +inf is past the end.
    captured = RunCaptured("awk",
        {R"(BEGIN { s = "hello"; print substr(s, 2), substr(s, 2, 3), )"
         R"(substr(s, 0), "[" substr(s, -1, 3) "]", substr(s, 1.5), )"
         R"(substr(s, 1.5, 2.3), "[" substr(s, 10) "]", "[" substr(s, 3, 0) "]", )"
         R"("[" substr(s, 3, -1) "]", substr(12345, 2, 2) })"});
    EXPECT_EQ(captured.out, "ello ell hello [hel] hello he [] [] [] 23\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk",
        {R"(BEGIN { s = "hello"; print substr(s, 2.5, 1), substr(s, 3.5, 1.5), )"
         R"(substr(s, 2.5, 2.5), substr(s, -2, 4), substr(s, 1, 1e30), )"
         R"(substr(s, -1e30, 1e30), substr(s, "x", 2), "[" substr(s, 1, 0.5) "]" })"});
    EXPECT_EQ(captured.out, "e l el hell hello hello he []\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk",
        {R"(BEGIN { s = "hello"; print substr(s, 2.7, 1), substr(s, 2, 1.7), )"
         R"(substr(s, -0.5, 3), "[" substr(s, 2, "nan") "]", )"
         R"("[" substr(s, "+inf") "]", substr(s, 2, "+inf") })"});
    EXPECT_EQ(captured.out, "e e hel [] [] ello\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // index finds the first occurrence, an empty t at 1.
    captured = RunCaptured("awk",
        {R"(BEGIN { print index("hello", "ll"), index("hello", ""), )"
         R"(index("", "a"), index(12345, 34), index("abc", "abcd") })"});
    EXPECT_EQ(captured.out, "3 1 0 3 0\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // The case functions touch ASCII letters only, bytes kept.
    captured = RunCaptured("awk",
        {R"(BEGIN { print toupper("abc\344x1"), tolower("ABC-Z"), toupper(1.5) })"});
    EXPECT_EQ(captured.out, "ABC\344X1 abc-z 1.5\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

// --- split ---

TEST_F(AwkFunctionsTest, Split) {
    // show joins the array's first n elements, so one line says both the
    // count and what split made of the text.
    const std::string show =
        R"(function show(n, a,  i, s) { s = n ":"; for (i = 1; i <= n; i++) )"
        R"(s = s "[" a[i] "]"; return s })";

    Captured captured = RunCaptured("awk", {show + R"(
BEGIN { print show(split("  a b\tc\n", a), a); print show(split("a:b::c", a, ":"), a); )"
R"(print show(split("a.b|c", a, "."), a); print show(split("abc", a, ""), a); )"
R"(print show(split("", a), a); print show(split("a1b22c", a, /[0-9]+/), a); )"
R"(print show(split(" a b ", a, / /), a); print show(split(" a  b ", a, "[ ]"), a); )"
R"(print show(split("abc", a, "b*"), a); print show(split("aXbxc", a, "x"), a) })"});
    EXPECT_EQ(captured.out, "3:[a][b][c]\n4:[a][b][][c]\n2:[a][b|c]\n3:[a][b][c]\n0:\n"
                            "3:[a][b][c]\n4:[][a][b][]\n5:[][a][][b][]\n2:[a][c]\n"
                            "2:[aXb][c]\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Without a separator, the current FS; split's "" is one piece per byte,
    // unlike FS ""; the pieces are strnum; the array is cleared first.
    captured = RunCaptured("awk", {show + R"(
BEGIN { FS = ","; print show(split("x,y z", a), a); FS = ""; print show(split("abc", a), a); )"
R"(n = split("1e3 2", b, " "); print (b[1] == 1000), (b[2] < 10); )"
R"(a["old"] = 1; split("q", a); print ("old" in a) })"});
    EXPECT_EQ(captured.out, "2:[x][y z]\n1:[abc]\n1 1\n0\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // The second argument must be a plain variable, not a scalar.
    const char* notAnArray =
        "awk: cmd. line:1: fatal: split: second argument is not an array\n";
    captured = RunCaptured("awk", {R"(BEGIN { x = 1; split("a b", x) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, notAnArray);
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {R"(BEGIN { split("a b", 3) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, notAnArray);
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {R"(BEGIN { split("a b", FS) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, notAnArray);
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk",
        {R"(function f(p) { p = 1; split("a", p) } BEGIN { f() })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, notAnArray);
    EXPECT_EQ(captured.status, 2);

    // gawk's fourth argument (the separators it made) is an extension.
    captured = RunCaptured("awk", {R"(BEGIN { split("a", x, " ", y) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: split: fourth argument is a "
                            "gawk extension\n");
    EXPECT_EQ(captured.status, 2);
}

// --- sub and gsub ---

TEST_F(AwkFunctionsTest, SubAndGsub) {
    // The replacement's `&' and `\'.
    Captured captured = RunCaptured("awk",
        {R"(BEGIN { r[1] = "&"; r[2] = "\\&"; r[3] = "\\\\&"; )"
         R"(r[4] = "\\\\\\&"; r[5] = "\\q"; r[6] = "\\\\q"; r[7] = "a\\"; )"
         R"(r[8] = "x&y&"; for (i = 1; i <= 8; i++) { s = "abc"; )"
         R"(sub(/b/, r[i], s); print i, s } })"});
    EXPECT_EQ(captured.out, "1 abc\n2 a&c\n3 a\\bc\n4 a\\&c\n5 a\\qc\n6 a\\qc\n"
                            "7 aa\\c\n8 axbybc\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // gsub's empty matches: one per position, but never two at the same
    // place (the byte there is copied and the search moves past it).
    captured = RunCaptured("awk",
        {R"(BEGIN { s = "aaa"; print gsub(/a/, "-&-", s), s; )"
         R"(s = "abc"; print gsub(/x*/, "-", s), s; )"
         R"(s = "hello"; print gsub(/l*/, "X", s), s; )"
         R"(s = "abab"; print gsub(/^a/, "X", s), s; )"
         R"(s = ""; print gsub(/^/, "X", s), s; )"
         R"(s = "abc"; print gsub(/$/, "X", s), s; )"
         R"(s = "a.b.c"; print gsub(".", "-", s), s; )"
         R"(s = "a.b.c"; print gsub("\\.", "-", s), s; )"
         R"(s = "aaa"; print sub(/a/, "b", s), s })"});
    EXPECT_EQ(captured.out, "3 -a--a--a-\n4 -a-b-c-\n4 XhXeXoX\n1 Xbab\n1 X\n"
                            "1 abcX\n5 -----\n2 a-b-c\n1 baa\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // On $0 by default: the record is re-split, so the fields and NF
    // follow; a field assignment rebuilds $0.
    captured = RunCaptured("awk",
        {R"({ print gsub(/o/, "0"), $0, NF, $2; $0 = "a b c"; )"
         R"(sub(/b/, "x y", $2); print $0, NF; $2 = "q"; )"
         R"(print gsub(/ /, "_"), $0, NF })"}, "one two three\n");
    EXPECT_EQ(captured.out, "2 0ne tw0 three 3 tw0\na x y c 3\n2 a_q_c 1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // An lvalue target is assigned the result as a string; without a
    // match it keeps its value and its type; a non-lvalue target is worked
    // on and dropped.
    captured = RunCaptured("awk",
        {R"(BEGIN { x = 15; print sub(/5/, "6", x), x, x + 1; )"
         R"(y = 0.1 + 0.2; print gsub(/z/, "", y), (y == 0.3); )"
         R"(z = 0.1 + 0.2; print gsub(/3/, "3", z), (z == 0.3); )"
         R"(a["k"] = "xx"; print gsub(/x/, "y", a["k"]), a["k"]; )"
         R"(print sub(/a/, "b", "abc"); n = sub(/^/, "X", u); print n, u })"});
    EXPECT_EQ(captured.out, "1 16 17\n0 0\n1 1\n2 yy\n1\n1 X\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A parenthesized regex literal is not the regex itself: it is the
    // value of `$0 ~ /re/', used as a dynamic regex.
    captured = RunCaptured("awk",
        {R"({ s = "a0b1"; n = sub((/b/), "X", s); print n, s; )"
         R"(print split("a1b0c", q, (/z/)), q[1]; print match("x1", (/b/)) })"},
        "b\n");
    EXPECT_EQ(captured.out, "1 a0bX\n2 a1b\n2\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // An array target, and a bad dynamic regex.
    captured = RunCaptured("awk", {R"(BEGIN { x[1]; sub(/a/, "b", x) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: attempt to use array `x' in a "
                            "scalar context\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {R"(BEGIN { s = "a+b"; gsub("+", "-", s) })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: invalid regexp: Invalid preceding "
                            "regular expression: /+/\n");
    EXPECT_EQ(captured.status, 2);
}

// --- match ---

TEST_F(AwkFunctionsTest, Match) {
    // match sets RSTART and RLENGTH: 1 and the length, or 0 and -1.
    Captured captured = RunCaptured("awk",
        {R"(BEGIN { print match("foobar", /o+/), RSTART, RLENGTH; )"
         R"(print match("foobar", "z"), RSTART, RLENGTH; )"
         R"(print match("foobar", /x*/), RSTART, RLENGTH; )"
         R"(print match("foobar", /$/), RSTART, RLENGTH; )"
         R"(print match(12345, 3), RSTART, RLENGTH; )"
         R"(r = "b.r"; print match("foobar", r), RSTART, RLENGTH; )"
         R"(print match("xabcabc", /(abc)+/), RSTART, RLENGTH })"});
    EXPECT_EQ(captured.out, "2 2 2\n0 0 -1\n1 1 0\n7 7 0\n3 3 1\n4 4 3\n2 2 6\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

// --- the argument counts, checked at parse time ---

TEST_F(AwkFunctionsTest, ArgumentCountErrors) {
    // gawk's two-line report: the program line, then the caret at the
    // call's ')' (column 0-based) with the message.
    const auto countError = [](const std::string& program, int column,
                               const std::string& message) {
        return "awk: cmd. line:1: " + program + "\n"
             + "awk: cmd. line:1: " + std::string(column, ' ') + "^ " + message + "\n";
    };

    struct Case {
        const char* program;
        int column;
        const char* message;
    };
    const Case cases[] = {
        {R"(BEGIN{print substr()})", 19, "0 is invalid as number of arguments for substr"},
        {R"(BEGIN{print substr("abc")})", 24,
         "1 is invalid as number of arguments for substr"},
        {R"(BEGIN{print length(1,2)})", 22, "2 is invalid as number of arguments for length"},
        {R"(BEGIN{split("a",x," ",y,z)})", 25, "5 is invalid as number of arguments for split"},
        {R"(BEGIN{print atan2(1)})", 19, "1 is invalid as number of arguments for atan2"},
        {R"(BEGIN{gsub(/a/,"b",c,d)})", 22, "4 is invalid as number of arguments for gsub"},
        {R"(BEGIN{rand(1)})", 12, "1 is invalid as number of arguments for rand"},
        {R"(BEGIN{print "x"; print match("a",/a/,m)})", 38,
         "match: third argument is a gawk extension"},
        {R"(BEGIN{print "x"; close("a","b")})", 30,
         "close: second argument is a gawk extension"},
    };
    for (const Case& c : cases) {
        Captured captured = RunCaptured("awk", {c.program});
        EXPECT_EQ(captured.out, "");
        EXPECT_EQ(captured.err, countError(c.program, c.column, c.message));
        EXPECT_EQ(captured.status, 1);
    }

    // A call across a newline: newlines are skipped after a comma, so the
    // ')' is on the second line, reported there.
    Captured captured = RunCaptured("awk", {"BEGIN{x = substr(\"abc\", 1,\n2, 3)}"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:2: 2, 3)}\n"
                            "awk: cmd. line:2:     ^ 4 is invalid as number of "
                            "arguments for substr\n");
    EXPECT_EQ(captured.status, 1);
}

} // namespace
} // namespace Haisos