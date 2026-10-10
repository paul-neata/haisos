#include "commands/awk/AwkParser.h"

#include <set>

namespace Haisos::Awk {

namespace {

ExprOp BinaryOpOf(TokenKind kind) {
    switch (kind) {
        case TokenKind::Plus:         return ExprOp::Add;
        case TokenKind::Minus:        return ExprOp::Subtract;
        case TokenKind::Star:         return ExprOp::Multiply;
        case TokenKind::Slash:        return ExprOp::Divide;
        case TokenKind::Percent:      return ExprOp::Modulo;
        case TokenKind::Caret:        return ExprOp::Power;
        case TokenKind::Less:         return ExprOp::Less;
        case TokenKind::LessEqual:    return ExprOp::LessEqual;
        case TokenKind::Equal:        return ExprOp::Equal;
        case TokenKind::NotEqual:     return ExprOp::NotEqual;
        case TokenKind::Greater:      return ExprOp::Greater;
        case TokenKind::GreaterEqual: return ExprOp::GreaterEqual;
        case TokenKind::Tilde:        return ExprOp::Match;
        case TokenKind::NoMatch:      return ExprOp::NoMatch;
        case TokenKind::Or:           return ExprOp::Or;
        case TokenKind::And:          return ExprOp::And;
        default:                      return ExprOp::None;
    }
}

ExprOp AssignOpOf(TokenKind kind) {
    switch (kind) {
        case TokenKind::Assign:    return ExprOp::Assign;
        case TokenKind::AddAssign: return ExprOp::AddAssign;
        case TokenKind::SubAssign: return ExprOp::SubtractAssign;
        case TokenKind::MulAssign: return ExprOp::MultiplyAssign;
        case TokenKind::DivAssign: return ExprOp::DivideAssign;
        case TokenKind::ModAssign: return ExprOp::ModuloAssign;
        case TokenKind::PowAssign: return ExprOp::PowerAssign;
        default:                   return ExprOp::None;
    }
}

} // namespace

Parser::Parser(const AwkSource& source, int sourceIndex)
    : m_lexer(source), m_sourceIndex(sourceIndex) {
    m_fileSource = source.name != kCommandLineSourceName;
    m_token = m_lexer.Next();
}

bool Parser::IsAssignOp(TokenKind kind) const {
    switch (kind) {
        case TokenKind::Assign:
        case TokenKind::AddAssign:
        case TokenKind::SubAssign:
        case TokenKind::MulAssign:
        case TokenKind::DivAssign:
        case TokenKind::ModAssign:
        case TokenKind::PowAssign:
            return true;
        default:
            return false;
    }
}

bool Parser::StartsConcatOperand(TokenKind kind) const {
    switch (kind) {
        case TokenKind::Number:
        case TokenKind::String:
        case TokenKind::Name:
        case TokenKind::FuncName:
        case TokenKind::Builtin:
        case TokenKind::Dollar:
        case TokenKind::Not:
        case TokenKind::LeftParen:
        case TokenKind::Increment:
        case TokenKind::Decrement:
        case TokenKind::Getline:
            return true;
        default:
            return false;
    }
}

ExprPtr Parser::MakeExpr(ExprKind kind, const Token& token) {
    ExprPtr expr = std::make_unique<Expr>();
    expr->kind = kind;
    expr->position = SourcePosition{m_sourceIndex, token.line};
    return expr;
}

ExprPtr Parser::MakeBinary(ExprOp op, ExprPtr left, ExprPtr right) {
    ExprPtr expr = std::make_unique<Expr>();
    expr->kind = ExprKind::Binary;
    expr->op = op;
    expr->position = left->position;
    expr->operands.push_back(std::move(left));
    expr->operands.push_back(std::move(right));
    return expr;
}

ExprPtr Parser::MakeAssign(ExprOp op, ExprPtr target, ExprPtr value) {
    ExprPtr expr = std::make_unique<Expr>();
    expr->kind = ExprKind::Assign;
    expr->op = op;
    expr->position = target->position;
    expr->operands.push_back(std::move(target));
    expr->operands.push_back(std::move(value));
    return expr;
}

void Parser::Fail(const Token& token, const std::string& message) {
    const std::string& sourceName = m_lexer.Source().name;
    if (token.kind == TokenKind::EndOfInput && m_programMode && m_fileSource) {
        // gawk's end-of-input-inside-a-rule report. Its caret is always put
        // at column 0 here: gawk's sits at the end of the last line when the
        // file has no final newline.
        throw AwkSyntaxError(
            "source files / command-line arguments must contain complete functions or rules",
            sourceName, token.line, "(END OF FILE)", 0);
    }
    if (token.kind == TokenKind::Newline) {
        if (!token.text.empty()) {
            // A newline that ends a comment: the caret goes on the
            // comment's '#', and the message is "syntax error".
            throw AwkSyntaxError("syntax error", sourceName, token.line + 1,
                                 m_lexer.LineText(token.line),
                                 token.column - token.text.size());
        }
        throw AwkSyntaxError("unexpected newline or end of string", sourceName, token.line + 1,
                            m_lexer.LineText(token.line), token.column);
    }
    if (token.kind == TokenKind::EndOfInput) {
        throw AwkSyntaxError("unexpected newline or end of string", sourceName, token.line,
                             m_lexer.LineText(token.line), token.column);
    }
    throw AwkSyntaxError(message, sourceName, token.line, m_lexer.LineText(token.line),
                         token.column);
}

ExprPtr Parser::ParseExpression() {
    ExprPtr expr = ParseAssignment(ParseTernary());
    if (IsAssignOp(m_token.kind)) {
        // Only an lvalue is assigned to: 3 = 4, (x-- = 2).
        Fail(m_token, "syntax error");
    }
    return expr;
}

ExprPtr Parser::ParseAssignment(ExprPtr left) {
    if (!IsAssignOp(m_token.kind)) {
        return left;
    }
    if (IsLvalue(*left)) {
        const ExprOp op = AssignOpOf(m_token.kind);
        Advance();
        return MakeAssign(op, std::move(left), ParseExpression());
    }
    // gawk takes the value before refusing a post-increment of a field.
    if (left->kind == ExprKind::IncDec &&
        (left->op == ExprOp::PostIncrement || left->op == ExprOp::PostDecrement) &&
        left->operands[0]->kind == ExprKind::Field) {
        Advance();
        ParseExpression();
        Fail(m_token,
             "cannot assign a value to the result of a field post-increment expression");
    }
    return left;
}

ExprPtr Parser::ParseTernary() {
    ExprPtr condition = ParseOr();
    if (m_token.kind != TokenKind::Question) {
        return condition;
    }
    Advance();
    // Either branch may be an assignment, as gawk's: c ? x = 1 : y = 2.
    ExprPtr ifTrue = ParseExpression();
    if (m_token.kind != TokenKind::Colon) {
        Fail(m_token, "syntax error");
    }
    Advance();
    ExprPtr ifFalse = ParseExpression();
    ExprPtr expr = std::make_unique<Expr>();
    expr->kind = ExprKind::Conditional;
    expr->position = condition->position;
    expr->operands.push_back(std::move(condition));
    expr->operands.push_back(std::move(ifTrue));
    expr->operands.push_back(std::move(ifFalse));
    return expr;
}

ExprPtr Parser::ParseOr() {
    ExprPtr left = ParseAnd();
    while (m_token.kind == TokenKind::Or) {
        Advance();
        left = MakeBinary(ExprOp::Or, std::move(left), ParseAssignment(ParseAnd()));
    }
    return left;
}

ExprPtr Parser::ParseAnd() {
    ExprPtr left = ParseIn();
    while (m_token.kind == TokenKind::And) {
        Advance();
        left = MakeBinary(ExprOp::And, std::move(left), ParseAssignment(ParseIn()));
    }
    return left;
}

ExprPtr Parser::ParseIn() {
    ExprPtr left = ParseMatch();
    while (m_token.kind == TokenKind::In) {
        Advance();
        if (m_token.kind != TokenKind::Name) {
            Fail(m_token, "syntax error");
        }
        const std::string arrayName = m_token.text;
        const SourcePosition arrayPosition{m_sourceIndex, m_token.line};
        Advance();
        ExprPtr expr = std::make_unique<Expr>();
        expr->kind = ExprKind::In;
        expr->position = left->position;
        expr->text = arrayName;
        expr->operands.push_back(std::move(left));
        AddNameUse(arrayName, arrayPosition);
        left = std::move(expr);
    }
    return left;
}

ExprPtr Parser::ParseMatch() {
    ExprPtr left = ParseComparison();
    while (m_token.kind == TokenKind::Tilde || m_token.kind == TokenKind::NoMatch) {
        const ExprOp op = m_token.kind == TokenKind::Tilde ? ExprOp::Match : ExprOp::NoMatch;
        Advance();
        left = MakeBinary(op, std::move(left), ParseAssignment(ParseComparison()));
    }
    return left;
}

ExprPtr Parser::ParseComparison() {
    ExprPtr left = ParseGetlineCommand();
    if (m_printContext && m_token.kind == TokenKind::Greater) {
        // print/printf arguments: '>' is a redirection, not a comparison.
        return left;
    }
    switch (m_token.kind) {
        case TokenKind::Less:
        case TokenKind::LessEqual:
        case TokenKind::Equal:
        case TokenKind::NotEqual:
        case TokenKind::Greater:
        case TokenKind::GreaterEqual:
            break;
        default:
            return left;
    }
    const ExprOp op = BinaryOpOf(m_token.kind);
    // The comparison operators are non-associative: nothing follows them.
    Advance();
    return MakeBinary(op, std::move(left), ParseAssignment(ParseGetlineCommand()));
}

ExprPtr Parser::ParseGetlineCommand() {
    ExprPtr command = ParseConcatenation();
    // print/printf arguments: '|' is a redirection, not '| getline'.
    while (m_token.kind == TokenKind::Pipe && !m_printContext) {
        Advance();
        if (m_token.kind != TokenKind::Getline) {
            Fail(m_token, "syntax error");
        }
        Advance();
        ExprPtr expr = std::make_unique<Expr>();
        expr->kind = ExprKind::Getline;
        expr->position = command->position;
        expr->getlineForm = GetlineForm::Command;
        expr->operands.push_back(std::move(command));
        expr->target = ParseGetlineTarget();
        command = std::move(expr);
        // Concatenation goes on after it, as gawk's: "cmd" | getline x "b".
        while (StartsConcatOperand(m_token.kind)) {
            command = MakeBinary(ExprOp::Concat, std::move(command), ParseAdditive());
        }
    }
    return command;
}

ExprPtr Parser::ParseConcatenation() {
    ExprPtr left = ParseAdditive();
    while (StartsConcatOperand(m_token.kind)) {
        // '+' and '-' are never a concatenation's start: a - 1 subtracts.
        left = MakeBinary(ExprOp::Concat, std::move(left), ParseAdditive());
    }
    return left;
}

ExprPtr Parser::ParseAdditive() {
    ExprPtr left = ParseMultiplicative();
    while (m_token.kind == TokenKind::Plus || m_token.kind == TokenKind::Minus) {
        const ExprOp op = m_token.kind == TokenKind::Plus ? ExprOp::Add : ExprOp::Subtract;
        Advance();
        left = MakeBinary(op, std::move(left), ParseMultiplicative());
    }
    return left;
}

ExprPtr Parser::ParseMultiplicative() {
    ExprPtr left = ParseUnary();
    while (m_token.kind == TokenKind::Star || m_token.kind == TokenKind::Slash ||
           m_token.kind == TokenKind::Percent) {
        const ExprOp op = m_token.kind == TokenKind::Star    ? ExprOp::Multiply
                          : m_token.kind == TokenKind::Slash ? ExprOp::Divide
                                                            : ExprOp::Modulo;
        Advance();
        left = MakeBinary(op, std::move(left), ParseUnary());
    }
    return left;
}

ExprPtr Parser::ParseUnary() {
    if (m_pendingPrimary) {
        // A print argument continuing from a grouping: the grouping is the
        // leftmost operand, so a '-', '+' or '!' after it is not unary
        // (print (a) - 1 subtracts).
        return ParsePower();
    }
    ExprOp op = ExprOp::None;
    switch (m_token.kind) {
        case TokenKind::Not:   op = ExprOp::Not;        break;
        case TokenKind::Minus: op = ExprOp::Negate;     break;
        case TokenKind::Plus:  op = ExprOp::UnaryPlus;  break;
        default:
            return ParsePower();
    }
    const Token operation = m_token;
    Advance();
    ExprPtr expr = std::make_unique<Expr>();
    expr->kind = ExprKind::Unary;
    expr->op = op;
    expr->position = SourcePosition{m_sourceIndex, operation.line};
    expr->operands.push_back(ParseUnary());
    return expr;
}

ExprPtr Parser::ParsePower() {
    ExprPtr left = ParsePostfix();
    if (m_token.kind != TokenKind::Caret) {
        return left;
    }
    // The exponent at the unary level: right-associative, and -2^2 is -(2^2).
    Advance();
    return MakeBinary(ExprOp::Power, std::move(left), ParseUnary());
}

ExprPtr Parser::ParsePreIncDec() {
    const Token operation = m_token;
    const bool isIncrement = m_token.kind == TokenKind::Increment;
    Advance();
    const Token operand = m_token;
    ExprPtr lvalue = ParsePrimary();
    if (!IsLvalue(*lvalue)) {
        Fail(operand, "syntax error");
    }
    ExprPtr expr = std::make_unique<Expr>();
    expr->kind = ExprKind::IncDec;
    expr->op = isIncrement ? ExprOp::PreIncrement : ExprOp::PreDecrement;
    expr->position = SourcePosition{m_sourceIndex, operation.line};
    expr->operands.push_back(std::move(lvalue));
    return expr;
}

ExprPtr Parser::ParsePostfix() {
    // A pending grouping comes first: print (x) ++y concatenates a pre-increment.
    if (!m_pendingPrimary &&
        (m_token.kind == TokenKind::Increment || m_token.kind == TokenKind::Decrement)) {
        return ParsePreIncDec();
    }
    ExprPtr expr = ParsePrimary();
    if (IsLvalue(*expr) &&
        (m_token.kind == TokenKind::Increment || m_token.kind == TokenKind::Decrement)) {
        const bool isIncrement = m_token.kind == TokenKind::Increment;
        Advance();
        ExprPtr incDec = std::make_unique<Expr>();
        incDec->kind = ExprKind::IncDec;
        incDec->op = isIncrement ? ExprOp::PostIncrement : ExprOp::PostDecrement;
        incDec->position = expr->position;
        incDec->operands.push_back(std::move(expr));
        expr = std::move(incDec);
    }
    return expr;
}

std::vector<ExprPtr> Parser::ParseCallArguments() {
    // The print context ends here: inside a call's parentheses, '>' and '|'
    // are what they are anywhere else.
    const bool printContext = m_printContext;
    m_printContext = false;
    std::vector<ExprPtr> arguments;
    if (m_token.kind == TokenKind::RightParen) {
        m_printContext = printContext;
        return arguments;
    }
    arguments.push_back(ParseExpression());
    while (m_token.kind == TokenKind::Comma) {
        Advance();
        arguments.push_back(ParseExpression());
    }
    m_printContext = printContext;
    if (m_token.kind != TokenKind::RightParen) {
        Fail(m_token, "syntax error");
    }
    return arguments;
}

ExprPtr Parser::ParseDollarOperand() {
    if (m_token.kind == TokenKind::Increment || m_token.kind == TokenKind::Decrement) {
        return ParsePreIncDec();
    }
    switch (m_token.kind) {
        case TokenKind::Minus:
        case TokenKind::Plus:
        case TokenKind::Not: {
            const Token operation = m_token;
            const ExprOp op = m_token.kind == TokenKind::Minus ? ExprOp::Negate
                              : m_token.kind == TokenKind::Plus ? ExprOp::UnaryPlus
                                                               : ExprOp::Not;
            Advance();
            ExprPtr expr = std::make_unique<Expr>();
            expr->kind = ExprKind::Unary;
            expr->op = op;
            expr->position = SourcePosition{m_sourceIndex, operation.line};
            expr->operands.push_back(ParseDollarOperand());
            return expr;
        }
        default:
            return ParsePrimary();
    }
}

ExprPtr Parser::ParseGetlineTarget() {
    if (m_token.kind == TokenKind::Name) {
        return ParseVariableOrElement();
    }
    if (m_token.kind == TokenKind::Dollar) {
        return ParseField();
    }
    return nullptr;
}

ExprPtr Parser::ParseVariableOrElement() {
    const Token name = m_token;
    Advance();
    if (m_token.kind != TokenKind::LeftBracket) {
        ExprPtr expr = MakeExpr(ExprKind::Variable, name);
        expr->text = name.text;
        AddNameUse(expr->text, expr->position);
        return expr;
    }
    Advance();
    // The print context ends inside the subscripts.
    const bool printContext = m_printContext;
    m_printContext = false;
    std::vector<ExprPtr> subscripts;
    subscripts.push_back(ParseExpression());
    while (m_token.kind == TokenKind::Comma) {
        Advance();
        subscripts.push_back(ParseExpression());
    }
    m_printContext = printContext;
    if (m_token.kind != TokenKind::RightBracket) {
        Fail(m_token, "syntax error");
    }
    Advance();
    ExprPtr expr = MakeExpr(ExprKind::Index, name);
    expr->text = name.text;
    expr->operands = std::move(subscripts);
    AddNameUse(expr->text, expr->position);
    return expr;
}

ExprPtr Parser::ParseField() {
    const Token dollar = m_token;
    Advance();
    ExprPtr expr = MakeExpr(ExprKind::Field, dollar);
    expr->operands.push_back(ParseDollarOperand());
    return expr;
}

ExprPtr Parser::ParsePrimary() {
    if (m_pendingPrimary) {
        // The grouping a print argument continues from: its leftmost primary.
        ExprPtr primary = std::move(m_pendingPrimary);
        m_pendingPrimary.reset();
        return primary;
    }
    switch (m_token.kind) {
        case TokenKind::Number: {
            ExprPtr expr = MakeExpr(ExprKind::Number, m_token);
            expr->text = m_token.text;
            expr->number = m_token.number;
            Advance();
            return expr;
        }
        case TokenKind::String: {
            ExprPtr expr = MakeExpr(ExprKind::String, m_token);
            expr->text = m_token.text;
            Advance();
            return expr;
        }
        case TokenKind::Slash:
        case TokenKind::DivAssign: {
            // A '/' where an operand is expected starts a regex literal.
            m_token = m_lexer.ScanRegex(m_token);
            ExprPtr expr = MakeExpr(ExprKind::Regex, m_token);
            expr->text = m_token.text;
            Advance();
            return expr;
        }
        case TokenKind::LeftParen: {
            const Token open = m_token;
            Advance();
            // The print context ends inside the parentheses.
            const bool printContext = m_printContext;
            m_printContext = false;
            std::vector<ExprPtr> exprs;
            exprs.push_back(ParseExpression());
            while (m_token.kind == TokenKind::Comma) {
                Advance();
                exprs.push_back(ParseExpression());
            }
            if (m_token.kind != TokenKind::RightParen) {
                Fail(m_token, "syntax error");
            }
            Advance();
            m_printContext = printContext;
            if (exprs.size() == 1) {
                // A grouping keeps no node, but is never an lvalue, as
                // gawk's: (x) = 3 is a syntax error.
                exprs[0]->parenthesized = true;
                return std::move(exprs[0]);
            }
            // (i, j) in a: several expressions are `in` subscripts.
            if (m_token.kind != TokenKind::In) {
                Fail(m_token, "syntax error");
            }
            Advance();
            if (m_token.kind != TokenKind::Name) {
                Fail(m_token, "syntax error");
            }
            const std::string arrayName = m_token.text;
            const SourcePosition arrayPosition{m_sourceIndex, m_token.line};
            Advance();
            ExprPtr expr = std::make_unique<Expr>();
            expr->kind = ExprKind::In;
            expr->position = SourcePosition{m_sourceIndex, open.line};
            expr->text = arrayName;
            expr->operands = std::move(exprs);
            AddNameUse(arrayName, arrayPosition);
            return expr;
        }
        case TokenKind::Name:
            return ParseVariableOrElement();
        case TokenKind::FuncName: {
            const Token name = m_token;
            Advance();
            if (m_token.kind != TokenKind::LeftParen) {
                Fail(m_token, "syntax error");
            }
            Advance();
            std::vector<ExprPtr> arguments = ParseCallArguments();
            Advance();  // the ')'
            ExprPtr expr = MakeExpr(ExprKind::Call, name);
            expr->text = name.text;
            expr->operands = std::move(arguments);
            return expr;
        }
        case TokenKind::Builtin: {
            const Token name = m_token;
            Advance();
            if (m_token.kind != TokenKind::LeftParen) {
                // Only length comes without parentheses.
                if (name.text == "length") {
                    ExprPtr expr = MakeExpr(ExprKind::BuiltinCall, name);
                    expr->text = name.text;
                    expr->hasParentheses = false;
                    return expr;
                }
                Fail(m_token, "syntax error");
            }
            Advance();
            std::vector<ExprPtr> arguments = ParseCallArguments();
            Advance();  // the ')'
            ExprPtr expr = MakeExpr(ExprKind::BuiltinCall, name);
            expr->text = name.text;
            expr->operands = std::move(arguments);
            return expr;
        }
        case TokenKind::Dollar:
            return ParseField();
        case TokenKind::Getline: {
            const Token keyword = m_token;
            Advance();
            ExprPtr expr = MakeExpr(ExprKind::Getline, keyword);
            expr->target = ParseGetlineTarget();
            if (m_token.kind == TokenKind::Less) {
                Advance();
                // The file operand: arithmetic, unary and '^' included, but
                // no concatenation and no comparison.
                expr->getlineForm = GetlineForm::File;
                expr->operands.push_back(ParseAdditive());
            }
            return expr;
        }
        default:
            Fail(m_token, "syntax error");
    }
}

namespace {

// gawk's special variables, which a function parameter may not reuse.
bool IsSpecialVariable(const std::string& name) {
    static const char* const specials[] = {
        "ARGC", "ARGV", "CONVFMT", "ENVIRON", "FILENAME", "FNR", "FS", "NF",
        "NR", "OFMT", "OFS", "ORS", "RLENGTH", "RS", "RSTART", "SUBSEP",
    };
    for (const char* special : specials) {
        if (name == special) {
            return true;
        }
    }
    return false;
}

} // namespace

bool Parser::AtSimpleStatementEnd() const {
    switch (m_token.kind) {
        case TokenKind::Semicolon:
        case TokenKind::Newline:
        case TokenKind::RightBrace:
        case TokenKind::EndOfInput:
            return true;
        default:
            return false;
    }
}

void Parser::ExpectSimpleStatementEnd() {
    if (!AtSimpleStatementEnd()) {
        Fail(m_token, "syntax error");
    }
}

void Parser::Expect(TokenKind kind) {
    if (m_token.kind != kind) {
        Fail(m_token, "syntax error");
    }
    Advance();
}

void Parser::SkipNewlines() {
    while (m_token.kind == TokenKind::Newline) {
        Advance();
    }
}

void Parser::SkipStatementSeparators() {
    while (m_token.kind == TokenKind::Newline || m_token.kind == TokenKind::Semicolon) {
        Advance();
    }
}

StmtPtr Parser::MakeStmt(StmtKind kind, const Token& token) {
    StmtPtr stmt = std::make_unique<Stmt>();
    stmt->kind = kind;
    stmt->position = SourcePosition{m_sourceIndex, token.line};
    return stmt;
}

void Parser::AddError(const Token& token, const std::string& message) {
    FlushWarnings();
    m_failed = true;
    *m_diagnostics += FormatAwkError(m_lexer.Source().name, token.line, message);
}

void Parser::AddNameUse(const std::string& name, const SourcePosition& position) {
    if (m_variableUses == nullptr) {
        return;  // expression-only parsing (ParseAwkExpression) collects no uses
    }
    if (m_functionParameters != nullptr) {
        // A parameter of the enclosing function shadows the name.
        for (const std::string& parameter : *m_functionParameters) {
            if (parameter == name) {
                return;
            }
        }
    }
    m_variableUses->push_back(VariableUse{name, position});
}

void Parser::FlushWarnings() {
    for (const AwkWarning& warning : m_lexer.TakeWarnings()) {
        *m_diagnostics += FormatAwkWarning(warning);
    }
}

StmtPtr Parser::ParseBlock() {
    const Token open = m_token;
    StmtPtr block = MakeStmt(StmtKind::Block, open);
    Advance();  // '{'; the lexer skips the newlines after it
    while (true) {
        SkipStatementSeparators();
        if (m_token.kind == TokenKind::RightBrace) {
            Advance();
            return block;
        }
        block->statements.push_back(ParseStatement());
    }
}

StmtPtr Parser::ParseBody() {
    if (m_token.kind == TokenKind::Semicolon) {
        // A lone ';': an empty Block, the ';' left as any other terminator.
        return MakeStmt(StmtKind::Block, m_token);
    }
    return ParseStatement();
}

StmtPtr Parser::ParseStatement() {
    switch (m_token.kind) {
        case TokenKind::LeftBrace:
            return ParseBlock();
        case TokenKind::If: {
            StmtPtr stmt = MakeStmt(StmtKind::If, m_token);
            Advance();
            Expect(TokenKind::LeftParen);
            stmt->expr = ParseExpression();
            Expect(TokenKind::RightParen);
            SkipNewlines();
            stmt->body = ParseBody();
            // The ';' that ended a simple then-body is also what separates it
            // from an 'else'.
            SkipStatementSeparators();
            if (m_token.kind == TokenKind::Else) {
                Advance();  // the lexer skips the newlines after 'else'
                stmt->elseBody = ParseBody();
            }
            return stmt;
        }
        case TokenKind::While: {
            StmtPtr stmt = MakeStmt(StmtKind::While, m_token);
            Advance();
            Expect(TokenKind::LeftParen);
            stmt->expr = ParseExpression();
            Expect(TokenKind::RightParen);
            SkipNewlines();
            ++m_loopDepth;
            stmt->body = ParseBody();
            --m_loopDepth;
            return stmt;
        }
        case TokenKind::Do: {
            StmtPtr stmt = MakeStmt(StmtKind::Do, m_token);
            Advance();  // the lexer skips the newlines after 'do'
            ++m_loopDepth;
            stmt->body = ParseBody();
            --m_loopDepth;
            SkipStatementSeparators();
            Expect(TokenKind::While);
            Expect(TokenKind::LeftParen);
            stmt->expr = ParseExpression();
            Expect(TokenKind::RightParen);
            ExpectSimpleStatementEnd();
            return stmt;
        }
        case TokenKind::For: {
            StmtPtr stmt = MakeStmt(StmtKind::For, m_token);
            Advance();
            Expect(TokenKind::LeftParen);
            ExprPtr first;
            if (m_token.kind != TokenKind::Semicolon) {
                first = ParseExpression();
                // for (Name in name): an In of one plain variable, ')'
                // right after. Anything else is a classic for's init, and a
                // ';' must follow it (for ((k) in a) fails at its ')').
                if (first->kind == ExprKind::In && first->operands.size() == 1 &&
                    first->operands[0]->kind == ExprKind::Variable &&
                    !first->operands[0]->parenthesized &&
                    m_token.kind == TokenKind::RightParen) {
                    stmt->kind = StmtKind::ForIn;
                    stmt->name = first->operands[0]->text;
                    stmt->arrayName = first->text;
                    Advance();
                    SkipNewlines();
                    ++m_loopDepth;
                    stmt->body = ParseBody();
                    --m_loopDepth;
                    return stmt;
                }
                stmt->init = std::make_unique<Stmt>();
                stmt->init->kind = StmtKind::Expression;
                stmt->init->position = first->position;
                stmt->init->expr = std::move(first);
            }
            Expect(TokenKind::Semicolon);
            if (m_token.kind != TokenKind::Semicolon) {
                stmt->expr = ParseExpression();
            }
            Expect(TokenKind::Semicolon);
            if (m_token.kind != TokenKind::RightParen) {
                ExprPtr update = ParseExpression();
                stmt->update = std::make_unique<Stmt>();
                stmt->update->kind = StmtKind::Expression;
                stmt->update->position = update->position;
                stmt->update->expr = std::move(update);
            }
            Expect(TokenKind::RightParen);
            SkipNewlines();
            ++m_loopDepth;
            stmt->body = ParseBody();
            --m_loopDepth;
            return stmt;
        }
        case TokenKind::Print:
            return ParsePrintStatement(StmtKind::Print);
        case TokenKind::Printf:
            return ParsePrintStatement(StmtKind::Printf);
        case TokenKind::Next:
        case TokenKind::Nextfile: {
            const bool isNext = m_token.kind == TokenKind::Next;
            StmtPtr stmt = MakeStmt(isNext ? StmtKind::Next : StmtKind::Nextfile, m_token);
            if (m_beginEndAction != nullptr) {
                AddError(m_token, "`" + std::string(isNext ? "next" : "nextfile") +
                                      "' used in " + std::string(m_beginEndAction) + " action");
            }
            Advance();
            ExpectSimpleStatementEnd();
            return stmt;
        }
        case TokenKind::Break:
        case TokenKind::Continue: {
            const bool isBreak = m_token.kind == TokenKind::Break;
            StmtPtr stmt = MakeStmt(isBreak ? StmtKind::Break : StmtKind::Continue, m_token);
            if (m_loopDepth == 0) {
                // gawk prints these twice; once here.
                AddError(m_token, isBreak
                                      ? "`break' is not allowed outside a loop or switch"
                                      : "`continue' is not allowed outside a loop");
            }
            Advance();
            ExpectSimpleStatementEnd();
            return stmt;
        }
        case TokenKind::Exit:
        case TokenKind::Return: {
            const bool isReturn = m_token.kind == TokenKind::Return;
            if (isReturn && m_functionDepth == 0) {
                Fail(m_token, "`return' used outside function context");
            }
            StmtPtr stmt = MakeStmt(isReturn ? StmtKind::Return : StmtKind::Exit, m_token);
            Advance();
            if (!AtSimpleStatementEnd()) {
                stmt->expr = ParseExpression();
            }
            ExpectSimpleStatementEnd();
            return stmt;
        }
        case TokenKind::Delete: {
            StmtPtr stmt = MakeStmt(StmtKind::Delete, m_token);
            Advance();
            if (m_token.kind != TokenKind::Name) {
                Fail(m_token, "syntax error");
            }
            stmt->name = m_token.text;
            AddNameUse(stmt->name, SourcePosition{m_sourceIndex, m_token.line});
            Advance();
            if (m_token.kind == TokenKind::LeftBracket) {
                Advance();
                const bool printContext = m_printContext;
                m_printContext = false;
                stmt->args.push_back(ParseExpression());
                while (m_token.kind == TokenKind::Comma) {
                    Advance();
                    stmt->args.push_back(ParseExpression());
                }
                m_printContext = printContext;
                Expect(TokenKind::RightBracket);
            }
            ExpectSimpleStatementEnd();
            return stmt;
        }
        default: {
            StmtPtr stmt = MakeStmt(StmtKind::Expression, m_token);
            stmt->expr = ParseExpression();
            ExpectSimpleStatementEnd();
            return stmt;
        }
    }
}

StmtPtr Parser::ParsePrintStatement(StmtKind kind) {
    StmtPtr stmt = MakeStmt(kind, m_token);
    Advance();
    // What ends the argument list: a statement terminator, or a redirection.
    const auto EndsArguments = [this]() {
        switch (m_token.kind) {
            case TokenKind::Greater:
            case TokenKind::Append:
            case TokenKind::Pipe:
                return true;
            default:
                return AtSimpleStatementEnd();
        }
    };
    std::vector<ExprPtr> args;
    if (m_token.kind == TokenKind::LeftParen) {
        // print ( ... ): the arguments -- or a grouping of the first, or 'in'
        // subscripts; which one is decided by what follows the ')'.
        const Token open = m_token;
        Advance();
        const bool printContext = m_printContext;
        m_printContext = false;
        std::vector<ExprPtr> exprs;
        exprs.push_back(ParseExpression());
        while (m_token.kind == TokenKind::Comma) {
            Advance();
            exprs.push_back(ParseExpression());
        }
        m_printContext = printContext;
        Expect(TokenKind::RightParen);
        if (EndsArguments()) {
            args = std::move(exprs);
        } else if (exprs.size() == 1) {
            // A grouping: the first argument continues from it -- and being
            // parenthesized, it is never an lvalue (print (x) = 3 fails at
            // its '=').
            m_pendingPrimary = std::move(exprs[0]);
            m_pendingPrimary->parenthesized = true;
            m_printContext = true;
            args.push_back(ParseExpression());
        } else {
            // Several expressions are 'in' subscripts.
            Expect(TokenKind::In);
            if (m_token.kind != TokenKind::Name) {
                Fail(m_token, "syntax error");
            }
            const std::string arrayName = m_token.text;
            const SourcePosition arrayPosition{m_sourceIndex, m_token.line};
            Advance();
            ExprPtr expr = std::make_unique<Expr>();
            expr->kind = ExprKind::In;
            expr->position = SourcePosition{m_sourceIndex, open.line};
            expr->text = arrayName;
            expr->operands = std::move(exprs);
            AddNameUse(arrayName, arrayPosition);
            args.push_back(std::move(expr));
        }
    } else if (!EndsArguments()) {
        m_printContext = true;
        args.push_back(ParseExpression());
    }
    while (m_token.kind == TokenKind::Comma) {
        Advance();
        m_printContext = true;
        args.push_back(ParseExpression());
    }
    m_printContext = false;
    stmt->args = std::move(args);
    // The redirection: '>' a file, '>>' an append, '|' a command, its target
    // at the concatenation level -- so no comparison and no ternary of its
    // own (print 1 > 2 ? "a" : "b" fails at the '?').
    switch (m_token.kind) {
        case TokenKind::Greater:
            stmt->redirect = RedirectKind::File;
            break;
        case TokenKind::Append:
            stmt->redirect = RedirectKind::Append;
            break;
        case TokenKind::Pipe:
            stmt->redirect = RedirectKind::Pipe;
            break;
        default:
            ExpectSimpleStatementEnd();
            return stmt;
    }
    Advance();
    m_printContext = true;
    stmt->redirectTarget = ParseConcatenation();
    m_printContext = false;
    ExpectSimpleStatementEnd();
    return stmt;
}

void Parser::ParseItem(Program& program) {
    const Token start = m_token;
    switch (m_token.kind) {
        case TokenKind::Function: {
            Item item;
            item.position = SourcePosition{m_sourceIndex, start.line};
            ParseFunctionItem(program, item);
            program.items.push_back(std::move(item));
            return;
        }
        case TokenKind::Begin:
        case TokenKind::End: {
            const bool isBegin = m_token.kind == TokenKind::Begin;
            Item item;
            item.kind = isBegin ? ItemKind::Begin : ItemKind::End;
            item.position = SourcePosition{m_sourceIndex, start.line};
            Advance();
            if (m_token.kind != TokenKind::LeftBrace) {
                Fail(m_token, "syntax error");
            }
            m_beginEndAction = isBegin ? "BEGIN" : "END";
            item.action = ParseBlock();
            m_beginEndAction = nullptr;
            program.items.push_back(std::move(item));
            return;
        }
        default: {
            Item item;
            item.kind = ItemKind::Main;
            item.position = SourcePosition{m_sourceIndex, start.line};
            if (m_token.kind == TokenKind::LeftBrace) {
                item.action = ParseBlock();
                program.items.push_back(std::move(item));
                return;
            }
            item.pattern = ParseExpression();
            if (m_token.kind == TokenKind::Comma) {
                Advance();
                item.rangeEnd = ParseExpression();
            }
            if (m_token.kind == TokenKind::LeftBrace) {
                item.action = ParseBlock();
            } else if (!AtSimpleStatementEnd()) {
                // A pattern alone is a whole item: the next one must start on
                // a new line, after a ';' or at the end.
                Fail(m_token, "syntax error");
            }
            program.items.push_back(std::move(item));
        }
    }
}

void Parser::ParseFunctionItem(Program& program, Item& item) {
    // m_token is 'function' or 'func'.
    Advance();
    const Token name = m_token;
    if (name.kind == TokenKind::Builtin) {
        Fail(name, "`" + name.text + "' is a built-in function, it cannot be redefined");
    }
    if (name.kind != TokenKind::Name && name.kind != TokenKind::FuncName) {
        Fail(name, "syntax error");
    }
    Advance();
    Expect(TokenKind::LeftParen);
    FunctionDefinition function;
    function.name = name.text;
    function.position = SourcePosition{m_sourceIndex, name.line};
    for (const FunctionDefinition& existing : program.functions) {
        if (existing.name == function.name) {
            AddError(name, "function name `" + function.name + "' previously defined");
            break;
        }
    }
    if (m_token.kind != TokenKind::RightParen) {
        while (true) {
            if (m_token.kind != TokenKind::Name) {
                Fail(m_token, "syntax error");
            }
            const Token parameter = m_token;
            for (size_t i = 0; i < function.parameters.size(); ++i) {
                if (function.parameters[i] == parameter.text) {
                    AddError(parameter,
                             "function `" + function.name + "': parameter #" +
                                 std::to_string(function.parameters.size() + 1) + ", `" +
                                 parameter.text + "', duplicates parameter #" +
                                 std::to_string(i + 1));
                    break;
                }
            }
            if (parameter.text == function.name) {
                AddError(parameter, "function `" + function.name +
                                        "': cannot use function name as parameter name");
            }
            if (IsSpecialVariable(parameter.text)) {
                AddError(parameter, "function `" + function.name +
                                        "': cannot use special variable `" + parameter.text +
                                        "' as a function parameter");
            }
            function.parameters.push_back(parameter.text);
            Advance();
            if (m_token.kind != TokenKind::Comma) {
                break;
            }
            Advance();
        }
    }
    Expect(TokenKind::RightParen);
    SkipNewlines();
    if (m_token.kind != TokenKind::LeftBrace) {
        Fail(m_token, "syntax error");
    }
    ++m_functionDepth;
    m_functionParameters = &function.parameters;
    function.body = ParseBlock();
    m_functionParameters = nullptr;
    --m_functionDepth;
    program.functions.push_back(std::move(function));
    item.kind = ItemKind::Function;
    item.functionIndex = static_cast<int>(program.functions.size()) - 1;
}

bool Parser::ParseSourceItems(Program& program, std::string& diagnostics,
                              std::vector<VariableUse>& variableUses) {
    m_diagnostics = &diagnostics;
    m_variableUses = &variableUses;
    m_programMode = true;
    SkipStatementSeparators();
    while (m_token.kind != TokenKind::EndOfInput) {
        ParseItem(program);
        FlushWarnings();
        SkipStatementSeparators();
    }
    FlushWarnings();
    return !m_failed;
}

ParseResult ParseAwkProgram(std::vector<AwkSource> sources) {
    ParseResult result;
    std::shared_ptr<Program> program = std::make_shared<Program>();
    program->sources = std::move(sources);
    std::vector<VariableUse> variableUses;
    bool failed = false;
    for (size_t i = 0; i < program->sources.size(); ++i) {
        Parser parser(program->sources[i], static_cast<int>(i));
        try {
            if (!parser.ParseSourceItems(*program, result.diagnostics, variableUses)) {
                failed = true;
            }
        } catch (const AwkSyntaxError& error) {
            parser.FlushWarnings();
            result.diagnostics += FormatAwkSyntaxError(error);
            result.failed = true;
            return result;  // the syntax error ends the program: nothing runs
        }
    }
    // After the whole program: a defined function's name used as a variable
    // or an array.
    std::set<std::string> functionNames;
    for (const FunctionDefinition& function : program->functions) {
        functionNames.insert(function.name);
    }
    for (const VariableUse& use : variableUses) {
        if (functionNames.count(use.name) == 0) {
            continue;
        }
        result.diagnostics += FormatAwkError(
            program->sources[use.position.source].name, use.position.line,
            "function `" + use.name + "' called with space between name and `(',\n"
                                      "or used as a variable or an array");
        failed = true;
    }
    if (failed) {
        result.failed = true;
        return result;
    }
    result.program = std::move(program);
    return result;
}

ExprPtr ParseAwkExpression(const std::string& text) {
    Parser parser(AwkSource{kCommandLineSourceName, text}, 0);
    ExprPtr expr = parser.ParseExpression();
    // The expression must be the whole text: the lexer's final Newline, then
    // EndOfInput.
    if (parser.m_token.kind != TokenKind::Newline) {
        parser.Fail(parser.m_token, "syntax error");
    }
    parser.Advance();
    if (parser.m_token.kind != TokenKind::EndOfInput) {
        parser.Fail(parser.m_token, "syntax error");
    }
    return expr;
}

} // namespace Haisos::Awk