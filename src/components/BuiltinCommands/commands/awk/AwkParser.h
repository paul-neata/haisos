#pragma once

#include <string>

#include "commands/awk/AwkAst.h"
#include "commands/awk/AwkLexer.h"

namespace Haisos::Awk {

// The recursive-descent parser of awk expressions, statements and whole
// programs (see "Parsing programs" in the awk CLAUDE.md), POSIX precedence
// and associativity, lowest first (one method per level): assignment, ternary,
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
// A name used as a variable or an array: where, for the check after the whole
// program that a defined function's name is not (gawk's "called with space
// between name and `(' ...").
struct VariableUse {
    std::string name;
    SourcePosition position;
};

// A whole program parsed: see "Parsing programs" in the awk CLAUDE.md.
struct ParseResult {
    // The parsed program (sources moved in); null when anything failed.
    std::shared_ptr<Program> program;
    // Everything to write to stderr, in the order found: warnings
    // (FormatAwkWarning), parse-time errors (FormatAwkError) and, last, the
    // syntax error that stopped parsing (FormatAwkSyntaxError).
    std::string diagnostics;
    bool failed = false;   // an error or a syntax error: exit status 1, nothing runs
};

// Parses every source's items into one Program, in order. A rule may not
// span two sources: each is parsed by its own Parser.
ParseResult ParseAwkProgram(std::vector<AwkSource> sources);

class Parser {
public:
    // Parses tokens of |source|, which is Program::sources[sourceIndex].
    Parser(const AwkSource& source, int sourceIndex);

    // One expression (`expr` of the POSIX grammar: assignment and ternary
    // included). Throws AwkSyntaxError.
    ExprPtr ParseExpression();

    // Parses this source's items (rules and functions) into |program|, in
    // source order, appending every warning and parse-time error, formatted,
    // to |diagnostics| and every variable or array name use to |variableUses|.
    // Returns false when a parse-time error was found. Throws AwkSyntaxError.
    bool ParseSourceItems(Program& program, std::string& diagnostics,
                          std::vector<VariableUse>& variableUses);

    // The warnings found so far, formatted into the diagnostics of the
    // ParseSourceItems call in progress.
    void FlushWarnings();

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
    // The current token is a Name: a Variable, or with '[' an Index of one
    // or more subscripts (']' required).
    ExprPtr ParseVariableOrElement();
    // The current token is a '$': a Field of a dollar operand.
    ExprPtr ParseField();
    // The arguments of a call: [ expression { ',' expression } ] ')' -- the
    // caller has consumed the '(' and checks the ')' itself.
    std::vector<ExprPtr> ParseCallArguments();
    // --- statements, items and programs (see "Parsing programs" in the awk
    // CLAUDE.md) ---
    // '{' statements '}', the caller has checked the '{'.
    StmtPtr ParseBlock();
    // One statement, and the body of if/while/for: a lone ';' is an empty
    // Block (the ';' stays, a terminator like any other).
    StmtPtr ParseStatement();
    StmtPtr ParseBody();
    // print/printf: arguments and '>' '>>' '|' redirection.
    StmtPtr ParsePrintStatement(StmtKind kind);
    // One item (a rule or a function definition).
    void ParseItem(Program& program);
    // 'function' Name '(' parameters ')' newlines block.
    void ParseFunctionItem(Program& program, Item& item);
    // ';', a newline, or nothing before '}' ends a simple statement.
    bool AtSimpleStatementEnd() const;
    void ExpectSimpleStatementEnd();
    void SkipNewlines();
    void SkipStatementSeparators();
    StmtPtr MakeStmt(StmtKind kind, const Token& token);
    // A parse-time error, gawk's: recorded, parsing goes on.
    void AddError(const Token& token, const std::string& message);
    // A variable or array name use, for the check after the whole program
    // (a parameter of the enclosing function shadows the name).
    void AddNameUse(const std::string& name, const SourcePosition& position);
    void Expect(TokenKind kind);

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
    // --- program parsing state ---
    bool m_programMode = false;     // ParseSourceItems is running
    bool m_fileSource = false;      // a -f source: EndOfInput inside an item is gawk's "(END OF FILE)" error
    std::string* m_diagnostics = nullptr;
    std::vector<VariableUse>* m_variableUses = nullptr;
    bool m_failed = false;          // a parse-time error was found
    bool m_printContext = false;    // print/printf arguments: '>' and '|' end the list
    ExprPtr m_pendingPrimary;       // the grouping a print argument continues from
    const char* m_beginEndAction = nullptr;  // "BEGIN"/"END" while such an action is parsed
    int m_loopDepth = 0;            // while/do/for bodies
    int m_functionDepth = 0;        // function bodies
    const std::vector<std::string>* m_functionParameters = nullptr;  // the enclosing function's
};

// For the tests: |text| (source "cmd. line") as one expression that must be
// the whole text (then the lexer's final Newline and EndOfInput). Throws
// AwkSyntaxError.
ExprPtr ParseAwkExpression(const std::string& text);

} // namespace Haisos::Awk