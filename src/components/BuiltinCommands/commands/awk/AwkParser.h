#pragma once

#include <string>

#include "commands/awk/AwkAst.h"
#include "commands/awk/AwkLexer.h"

namespace Haisos::Awk {

// The recursive-descent parser of awk expressions, POSIX precedence and
// associativity, lowest first (one method per level): assignment, ternary,
// '||', '&&', 'in', '~' and '!~', the comparison operators, '| getline',
// concatenation, '+' and '-', '*' '/' '%', unary '! - +', '^' (the exponent
// at the unary level, so right-associative), '++'/'--' (pre and post) and
// the primaries -- among them '$' (a field of a dollar operand: no postfix
// under it, so $i++ is ($i)++) and every 'getline' form.
//
// It keeps exactly one token of lookahead: m_token is the current token;
// Advance() fetches the next, and the lexer never reads past m_token -- so
// a '/' (or '/=') standing where an operand is expected can be handed back
// to Lexer::ScanRegex and become a regex literal; anywhere else it is
// division. A plain class (no interface), owned by value; its lexer is a
// member.
class Parser {
public:
    // Parses tokens of |source|, which is Program::sources[sourceIndex].
    Parser(const AwkSource& source, int sourceIndex);

    // One expression (`expr` of the POSIX grammar: assignment and ternary
    // included). Throws AwkSyntaxError.
    ExprPtr ParseExpression();

    // awk--parser adds the statement, item and program methods here.

private:
    friend ExprPtr ParseAwkExpression(const std::string& text);

    // An assignment to |left| when the current token is an assignment
    // operator and |left| is an lvalue (else |left| itself): after the
    // ternary, and -- as gawk's -- as the right operand of || && ~ !~ and the
    // comparisons (1 && y = 2 is 1 && (y = 2)).
    ExprPtr ParseAssignment(ExprPtr left);
    ExprPtr ParseTernary();
    ExprPtr ParseOr();
    ExprPtr ParseAnd();
    ExprPtr ParseIn();
    ExprPtr ParseMatch();
    ExprPtr ParseComparison();
    ExprPtr ParseGetlineCommand();
    ExprPtr ParseConcatenation();
    ExprPtr ParseAdditive();
    ExprPtr ParseMultiplicative();
    ExprPtr ParseUnary();
    ExprPtr ParsePower();
    ExprPtr ParsePostfix();
    ExprPtr ParsePrimary();
    // The operand of a '$': '++'/'--' on an lvalue, '-'/'+'/'!' then a
    // dollar operand, or a primary -- never with a postfix of its own.
    ExprPtr ParseDollarOperand();
    // '++'/'--' in front of an lvalue (the caller has checked the token):
    // anything but an lvalue is a syntax error at its first token.
    ExprPtr ParsePreIncDec();
    // getline's target: a name, name[subscripts] or $... when the current
    // token is a Name or '$'; null when there is none.
    ExprPtr ParseGetlineTarget();
    // The arguments of a call: [ expression { ',' expression } ] ')' -- the
    // caller has consumed the '(' and checks the ')' itself.
    std::vector<ExprPtr> ParseCallArguments();

    void Advance() { m_token = m_lexer.Next(); }
    bool IsAssignOp(TokenKind kind) const;
    bool StartsConcatOperand(TokenKind kind) const;
    ExprPtr MakeExpr(ExprKind kind, const Token& token);
    ExprPtr MakeBinary(ExprOp op, ExprPtr left, ExprPtr right);
    ExprPtr MakeAssign(ExprOp op, ExprPtr target, ExprPtr value);
    // gawk's report: the message at the token -- but a Newline is
    // "unexpected newline or end of string" on the line it ends (one more
    // than its own), a Newline ending a comment is "syntax error" with the
    // caret on the comment's '#', and EndOfInput is "unexpected newline or
    // end of string" at its own place.
    [[noreturn]] void Fail(const Token& token, const std::string& message);

    Lexer m_lexer;
    Token m_token;
    int m_sourceIndex = 0;
};

// For the tests: |text| (source "cmd. line") as one expression that must be
// the whole text (then the lexer's final Newline and EndOfInput). Throws
// AwkSyntaxError.
ExprPtr ParseAwkExpression(const std::string& text);

} // namespace Haisos::Awk