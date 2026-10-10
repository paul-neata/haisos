#pragma once

#include <memory>
#include <string>
#include <vector>

#include "commands/awk/AwkError.h"

namespace Haisos::Awk {

// Where a node starts: the index of its AwkSource in Program::sources, and
// its line there (from 1). Runtime errors report it.
struct SourcePosition {
    int source = 0;
    int line = 0;
};

struct Expr;
struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

enum class ExprKind {
    Number,       // number, text: the source text ("1e3")
    String,        // text: the value
    Regex,         // text: the regex text as the lexer kept it; alone, it means $0 ~ /re/
    Variable,      // text: the name
    Field,         // $operands[0]
    Index,         // text[operands...]: the array's name, the subscripts (>= 1)
    In,            // (operands...) in text: the subscripts (>= 1), the array's name
    Unary,         // op Negate / UnaryPlus / Not; operands[0]
    Binary,        // op Add ... Or, Concat, Match, NoMatch; operands[0], operands[1]
    Conditional,   // operands[0] ? operands[1] : operands[2]
    Assign,        // op Assign ... PowAssign; operands[0] an lvalue, operands[1] the value
    IncDec,        // op PreIncrement ... PostDecrement; operands[0] an lvalue
    Call,          // a user function: text the name, operands the arguments
    BuiltinCall,   // text the built-in's name, operands the arguments; hasParentheses
    Getline,       // getlineForm; target (null: $0, NR...); operands[0] the file or command (File/Command)
};

enum class ExprOp {
    None,
    Add, Subtract, Multiply, Divide, Modulo, Power, Concat,
    Less, LessEqual, NotEqual, Equal, Greater, GreaterEqual, Match, NoMatch, And, Or,
    Negate, UnaryPlus, Not,
    Assign, AddAssign, SubtractAssign, MultiplyAssign, DivideAssign, ModuloAssign, PowerAssign,
    PreIncrement, PreDecrement, PostIncrement, PostDecrement,
};

enum class GetlineForm { Simple, File, Command };  // getline [var]; getline [var] < file; cmd | getline [var]

struct Expr {
    ExprKind kind = ExprKind::Number;
    ExprOp op = ExprOp::None;
    SourcePosition position;
    double number = 0;
    std::string text;
    std::vector<ExprPtr> operands;
    bool hasParentheses = true;            // BuiltinCall: false only for a bare `length`
    GetlineForm getlineForm = GetlineForm::Simple;
    ExprPtr target;                        // Getline's variable, field or element; null when none
};

// An lvalue: Variable, Field or Index.
bool IsLvalue(const Expr& expr);

enum class StmtKind {
    Expression, Print, Printf, If, While, Do, For, ForIn, Block,
    Next, Nextfile, Exit, Return, Break, Continue, Delete,
};

enum class RedirectKind { None, File /* > */, Append /* >> */, Pipe /* | */ };

struct Stmt {
    StmtKind kind = StmtKind::Block;
    SourcePosition position;
    ExprPtr expr;                     // Expression; If/While/Do condition; For condition (null: none); Exit/Return value (null: none)
    std::vector<ExprPtr> args;        // Print/Printf arguments (empty: print $0); Delete subscripts (empty: the whole array)
    RedirectKind redirect = RedirectKind::None;
    ExprPtr redirectTarget;           // Print/Printf with a redirection
    StmtPtr init;                     // For: a simple statement, or null
    StmtPtr update;                   // For: a simple statement, or null
    StmtPtr body;                     // If (then) / While / Do / For / ForIn
    StmtPtr elseBody;                 // If: null when no else
    std::vector<StmtPtr> statements;  // Block
    std::string name;                 // ForIn: the loop variable; Delete: the array
    std::string arrayName;            // ForIn: the array
};

enum class ItemKind { Begin, End, Main, Function };

struct FunctionDefinition {
    std::string name;
    std::vector<std::string> parameters;
    StmtPtr body;                     // a Block
    SourcePosition position;
};

struct Item {
    ItemKind kind = ItemKind::Main;
    ExprPtr pattern;                  // Main: null for an action alone
    ExprPtr rangeEnd;                 // Main: the second pattern of `p1, p2`; null otherwise
    StmtPtr action;                   // a Block; null for a pattern alone (print $0)
    int functionIndex = -1;           // Function: index in Program::functions
    SourcePosition position;
};

struct Program {
    std::vector<AwkSource> sources;
    std::vector<Item> items;                      // in source order, functions included
    std::vector<FunctionDefinition> functions;    // in source order
};

// The one-line dumps the tests compare against (see "AST dump" in the awk
// CLAUDE.md): one line per node, sub-nodes in parentheses.
std::string DumpExpr(const Expr& expr);
std::string DumpStmt(const Stmt& stmt);
std::string DumpProgram(const Program& program);   // one line per item, joined by '\n', no final newline

} // namespace Haisos::Awk