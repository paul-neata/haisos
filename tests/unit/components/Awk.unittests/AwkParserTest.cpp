#include <gtest/gtest.h>
#include <memory>
#include <string>
#include "commands/awk/AwkAst.h"
#include "commands/awk/AwkError.h"
#include "commands/awk/AwkParser.h"

namespace {

using Haisos::Awk::AwkSyntaxError;
using Haisos::Awk::ExprPtr;

// |text| parsed as one expression that must be the whole text, and its dump.
void ExpectExpr(const std::string& text, const std::string& dump) {
    ExprPtr expr;
    try {
        expr = Haisos::Awk::ParseAwkExpression(text);
    } catch (const AwkSyntaxError& error) {
        FAIL() << text << ": " << Haisos::Awk::FormatAwkSyntaxError(error);
    }
    EXPECT_EQ(Haisos::Awk::DumpExpr(*expr), dump) << text;
}

// |text| parsed as one expression, expecting the AwkSyntaxError with the
// given message, line and column.
void ExpectExprError(const std::string& text, const std::string& message, int line,
                     size_t column) {
    try {
        const ExprPtr expr = Haisos::Awk::ParseAwkExpression(text);
        FAIL() << "no AwkSyntaxError from " << text << " ("
               << Haisos::Awk::DumpExpr(*expr) << ")";
    } catch (const AwkSyntaxError& error) {
        EXPECT_EQ(std::string(error.what()), message) << text;
        EXPECT_EQ(error.Line(), line) << text;
        EXPECT_EQ(error.Column(), column) << text;
    }
}

} // namespace

TEST(AwkParserTest, ArithmeticPrecedence) {
    ExpectExpr("1 + 2 * 3", "(+ 1 (* 2 3))");
    ExpectExpr("2 ^ 3 ^ 2", "(^ 2 (^ 3 2))");
    ExpectExpr("-2 ^ 2", "(- (^ 2 2))");
    ExpectExpr("2 ^ -1", "(^ 2 (- 1))");
    ExpectExpr("a / b / c", "(/ (/ a b) c)");
    ExpectExpr("7 % 3 * 2", "(* (% 7 3) 2)");
    ExpectExpr("!x * 2", "(* (! x) 2)");
    ExpectExpr("- - x", "(- (- x))");
    ExpectExpr("x++ + ++y", "(+ (post++ x) (pre++ y))");
}

TEST(AwkParserTest, Concatenation) {
    ExpectExpr("a b c", "(concat (concat a b) c)");
    ExpectExpr("a \" \" b + 1", "(concat (concat a \" \") (+ b 1))");
    ExpectExpr("a -1", "(- a 1)");
    ExpectExpr("a (-1)", "(concat a (- 1))");
    ExpectExpr("x !y", "(concat x (! y))");
    ExpectExpr("a < b c", "(< a (concat b c))");
    ExpectExpr("f (1)", "(concat f 1)");
    ExpectExpr("1 ++x", "(concat 1 (pre++ x))");
    ExpectExpr("x ++ y", "(concat (post++ x) y)");
    ExpectExpr("\"a\" getline", "(concat \"a\" (getline))");
    ExpectExpr("1 getline < \"f\"", "(concat 1 (getline < \"f\"))");
}

TEST(AwkParserTest, MatchInLogical) {
    ExpectExpr("a ~ b c", "(~ a (concat b c))");
    ExpectExpr("a ~ /re/ && b", "(&& (~ a /re/) b)");
    ExpectExpr("a !~ \"x\"", "(!~ a \"x\")");
    ExpectExpr("k in a && x", "(&& (in a k) x)");
    ExpectExpr("(i, j) in a", "(in a i j)");
    ExpectExpr("x ~ y in a", "(in a (~ x y))");
    ExpectExpr("a || b && c", "(|| a (&& b c))");
    ExpectExpr("a ~ b ~ c", "(~ (~ a b) c)");
    ExpectExpr("1 < 2 in a", "(in a (< 1 2))");
    ExpectExpr("\"b\" ~ \"a\" < \"b\"", "(~ \"b\" (< \"a\" \"b\"))");
}

TEST(AwkParserTest, AssignmentAndTernary) {
    ExpectExpr("x = y = 3", "(= x (= y 3))");
    ExpectExpr("x += y ? 1 : 2", "(+= x (?: y 1 2))");
    ExpectExpr("a ? b : c ? d : e", "(?: a b (?: c d e))");
    ExpectExpr("$1 = 2", "(= ($ 1) 2)");
    ExpectExpr("a[1, \"x\"] ^= 2", "(^= a[1, \"x\"] 2)");
    ExpectExpr("n /= 2", "(/= n 2)");
    // As gawk's: an assignment as a ternary branch, or as the right operand
    // of || && ~ !~ and the comparisons, its value a whole expression.
    ExpectExpr("1 ? y = 5 : 2", "(?: 1 (= y 5) 2)");
    ExpectExpr("0 ? 1 : y = 2", "(?: 0 1 (= y 2))");
    ExpectExpr("1 && y = 0 || 1", "(&& 1 (= y (|| 0 1)))");
    ExpectExpr("0 || y = 2", "(|| 0 (= y 2))");
    ExpectExpr("1 < y = 2 < 3", "(< 1 (= y (< 2 3)))");
    ExpectExpr("\"a\" !~ y = \"a\"", "(!~ \"a\" (= y \"a\"))");
}

TEST(AwkParserTest, Fields) {
    ExpectExpr("$i++", "(post++ ($ i))");
    ExpectExpr("$++i", "($ (pre++ i))");
    ExpectExpr("$NF-1", "(- ($ NF) 1)");
    ExpectExpr("$-1", "($ (- 1))");
    ExpectExpr("$$0", "($ ($ 0))");
    ExpectExpr("$a[1]", "($ a[1])");
    ExpectExpr("$(i+1)", "($ (+ i 1))");
}

TEST(AwkParserTest, RegexOrDivision) {
    ExpectExpr("/re/", "/re/");
    ExpectExpr("x = /=/", "(= x /=/)");
    ExpectExpr("1 /2/ 4", "(/ (/ 1 2) 4)");
    ExpectExpr("length / 2", "(/ length 2)");
    ExpectExpr("/a\\/b/", "/a\\/b/");
    ExpectExpr("!/x/", "(! /x/)");
    ExpectExpr("(/x/)", "/x/");
}

TEST(AwkParserTest, Calls) {
    ExpectExpr("length", "length");
    ExpectExpr("length()", "(length)");
    ExpectExpr("length($1)", "(length ($ 1))");
    ExpectExpr("substr(s, 2, 3)", "(substr s 2 3)");
    ExpectExpr("f(1, g(2))", "(call f 1 (call g 2))");
    ExpectExpr("f()", "(call f)");
    ExpectExpr("\"a\\tb\"", "\"a\\tb\"");
}

TEST(AwkParserTest, GetlineForms) {
    ExpectExpr("getline", "(getline)");
    ExpectExpr("getline x", "(getline x)");
    ExpectExpr("getline < \"f\"", "(getline < \"f\")");
    ExpectExpr("getline x < f g", "(concat (getline x < f) g)");
    ExpectExpr("getline $1", "(getline ($ 1))");
    ExpectExpr("getline a[1]", "(getline a[1])");
    ExpectExpr("\"cmd\" | getline", "(| \"cmd\" getline)");
    ExpectExpr("\"cmd\" | getline x > 0", "(> (| \"cmd\" getline x) 0)");
    ExpectExpr("\"a\" \"b\" | getline", "(| (concat \"a\" \"b\") getline)");
    ExpectExpr("1 < \"cmd\" | getline", "(< 1 (| \"cmd\" getline))");
    ExpectExpr("getline < \"a\" \"b\"", "(concat (getline < \"a\") \"b\")");
    ExpectExpr("getline < \"f\" + 1", "(getline < (+ \"f\" 1))");
    ExpectExpr("getline < \"f\" < 2", "(< (getline < \"f\") 2)");
    ExpectExpr("\"a\" | getline x | getline", "(| (| \"a\" getline x) getline)");
    ExpectExpr("\"a\" | getline x \"b\"", "(concat (| \"a\" getline x) \"b\")");
}

TEST(AwkParserTest, ExpressionErrors) {
    ExpectExprError("a + * b", "syntax error", 1, 4);
    ExpectExprError("1 < 2 < 3", "syntax error", 1, 6);
    ExpectExprError("3 = 4", "syntax error", 1, 2);
    ExpectExprError("substr + 1", "syntax error", 1, 7);
    ExpectExprError("(1, 2) + 3", "syntax error", 1, 7);
    ExpectExprError("++1", "syntax error", 1, 2);
    ExpectExprError("1 +", "unexpected newline or end of string", 2, 3);
    ExpectExprError("a[1", "unexpected newline or end of string", 2, 3);
    ExpectExprError("x | y", "syntax error", 1, 4);
    ExpectExprError("(x-- = 2)", "syntax error", 1, 5);
    ExpectExprError("($1++ = 2)",
                    "cannot assign a value to the result of a field post-increment expression",
                    1, 9);
    ExpectExprError("($1-- += 2)",
                    "cannot assign a value to the result of a field post-increment expression",
                    1, 10);
    ExpectExprError("$1++ = 2", "unexpected newline or end of string", 2, 8);
    ExpectExprError("1 + # c", "syntax error", 2, 4);
    ExpectExprError("1 + y = 3", "syntax error", 1, 6);
    ExpectExprError("0 ? 1 : 2 = 3", "syntax error", 1, 10);
    ExpectExprError("(1 && $1++ = 2)",
                    "cannot assign a value to the result of a field post-increment expression",
                    1, 14);
}

TEST(AwkParserTest, ErrorTextIsGawks) {
    try {
        Haisos::Awk::ParseAwkExpression("a + * b");
        FAIL();
    } catch (const AwkSyntaxError& error) {
        EXPECT_EQ(Haisos::Awk::FormatAwkSyntaxError(error),
                  "awk: cmd. line:1: a + * b\n"
                  "awk: cmd. line:1:     ^ syntax error\n");
    }
}

namespace {

using Haisos::Awk::Expr;
using Haisos::Awk::ExprKind;
using Haisos::Awk::Item;
using Haisos::Awk::Program;
using Haisos::Awk::Stmt;
using Haisos::Awk::StmtKind;
using Haisos::Awk::StmtPtr;

StmtPtr NewStmt(StmtKind kind) {
    StmtPtr stmt = std::make_unique<Stmt>();
    stmt->kind = kind;
    return stmt;
}

StmtPtr EmptyBlock() {
    return NewStmt(StmtKind::Block);
}

// A Block holding one Expression statement of the variable |name|.
StmtPtr BlockOf(const std::string& name) {
    StmtPtr block = NewStmt(StmtKind::Block);
    StmtPtr expression = NewStmt(StmtKind::Expression);
    expression->expr = Haisos::Awk::ParseAwkExpression(name);
    block->statements.push_back(std::move(expression));
    return block;
}

} // namespace

TEST(AwkParserTest, DumpsStatements) {
    // Blocks: { x; print } and an empty one.
    StmtPtr block = BlockOf("x");
    block->statements.push_back(NewStmt(StmtKind::Print));
    EXPECT_EQ(Haisos::Awk::DumpStmt(*block), "{ x; print }");
    EXPECT_EQ(Haisos::Awk::DumpStmt(*EmptyBlock()), "{ }");

    // print and printf, plain and redirected; exit and return values.
    StmtPtr print = NewStmt(StmtKind::Print);
    print->args.push_back(Haisos::Awk::ParseAwkExpression("a"));
    print->args.push_back(Haisos::Awk::ParseAwkExpression("b"));
    EXPECT_EQ(Haisos::Awk::DumpStmt(*print), "print a, b");
    StmtPtr printfPipe = NewStmt(StmtKind::Printf);
    printfPipe->args.push_back(Haisos::Awk::ParseAwkExpression("\"%d\\n\""));
    printfPipe->args.push_back(Haisos::Awk::ParseAwkExpression("x"));
    printfPipe->redirect = Haisos::Awk::RedirectKind::Pipe;
    printfPipe->redirectTarget = Haisos::Awk::ParseAwkExpression("t");
    EXPECT_EQ(Haisos::Awk::DumpStmt(*printfPipe), "printf \"%d\\n\", x | t");
    StmtPtr append = NewStmt(StmtKind::Print);
    append->redirect = Haisos::Awk::RedirectKind::Append;
    append->redirectTarget = Haisos::Awk::ParseAwkExpression("t");
    EXPECT_EQ(Haisos::Awk::DumpStmt(*append), "print >> t");

    // The control statements.
    StmtPtr ifElse = NewStmt(StmtKind::If);
    ifElse->expr = Haisos::Awk::ParseAwkExpression("c");
    ifElse->body = BlockOf("x");
    ifElse->elseBody = BlockOf("y");
    EXPECT_EQ(Haisos::Awk::DumpStmt(*ifElse), "if (c) { x } else { y }");
    StmtPtr whileLoop = NewStmt(StmtKind::While);
    whileLoop->expr = Haisos::Awk::ParseAwkExpression("c");
    whileLoop->body = BlockOf("x");
    EXPECT_EQ(Haisos::Awk::DumpStmt(*whileLoop), "while (c) { x }");
    StmtPtr doLoop = NewStmt(StmtKind::Do);
    doLoop->expr = Haisos::Awk::ParseAwkExpression("c");
    doLoop->body = BlockOf("x");
    EXPECT_EQ(Haisos::Awk::DumpStmt(*doLoop), "do { x } while (c)");
    StmtPtr forLoop = NewStmt(StmtKind::For);
    forLoop->body = BlockOf("x");
    EXPECT_EQ(Haisos::Awk::DumpStmt(*forLoop), "for (; ; ) { x }");
    StmtPtr forIn = NewStmt(StmtKind::ForIn);
    forIn->name = "k";
    forIn->arrayName = "a";
    forIn->body = BlockOf("x");
    EXPECT_EQ(Haisos::Awk::DumpStmt(*forIn), "for (k in a) { x }");
    StmtPtr exit = NewStmt(StmtKind::Exit);
    exit->expr = Haisos::Awk::ParseAwkExpression("e");
    EXPECT_EQ(Haisos::Awk::DumpStmt(*exit), "exit e");
    EXPECT_EQ(Haisos::Awk::DumpStmt(*NewStmt(StmtKind::Return)), "return");
    StmtPtr del = NewStmt(StmtKind::Delete);
    del->name = "a";
    del->args.push_back(Haisos::Awk::ParseAwkExpression("s1"));
    del->args.push_back(Haisos::Awk::ParseAwkExpression("s2"));
    EXPECT_EQ(Haisos::Awk::DumpStmt(*del), "delete a[s1, s2]");
    StmtPtr delArray = NewStmt(StmtKind::Delete);
    delArray->name = "a";
    EXPECT_EQ(Haisos::Awk::DumpStmt(*delArray), "delete a");
}

TEST(AwkParserTest, DumpsProgram) {
    Program program;
    Item begin;
    begin.kind = Haisos::Awk::ItemKind::Begin;
    begin.action = EmptyBlock();
    program.items.push_back(std::move(begin));
    Item pattern;
    pattern.pattern = Haisos::Awk::ParseAwkExpression("x");
    program.items.push_back(std::move(pattern));
    Item patternAction;
    patternAction.pattern = Haisos::Awk::ParseAwkExpression("p");
    patternAction.action = EmptyBlock();
    program.items.push_back(std::move(patternAction));
    Item action;
    action.action = EmptyBlock();
    program.items.push_back(std::move(action));
    Item range;
    range.pattern = Haisos::Awk::ParseAwkExpression("p");
    range.rangeEnd = Haisos::Awk::ParseAwkExpression("q");
    range.action = EmptyBlock();
    program.items.push_back(std::move(range));
    Haisos::Awk::FunctionDefinition function;
    function.name = "f";
    function.parameters = {"a", "b"};
    function.body = EmptyBlock();
    program.functions.push_back(std::move(function));
    Item item;
    item.kind = Haisos::Awk::ItemKind::Function;
    item.functionIndex = 0;
    program.items.push_back(std::move(item));
    Item end;
    end.kind = Haisos::Awk::ItemKind::End;
    end.action = EmptyBlock();
    program.items.push_back(std::move(end));
    EXPECT_EQ(Haisos::Awk::DumpProgram(program),
              "BEGIN { }\n"
              "x\n"
              "p { }\n"
              "{ }\n"
              "p, q { }\n"
              "function f(a, b) { }\n"
              "END { }");
}