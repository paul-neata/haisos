#include "commands/awk/AwkParser.h"

namespace Haisos::Awk {

namespace {

ExprOp BinaryOpOf(TokenKind kind) {
    switch (kind) {
        case TokenKind::Plus:         return ExprOp::Add;
        case TokenKind::Minus:        return ExprOp::Subtract;
        case TokenKind::Star:        return ExprOp::Multiply;
        case TokenKind::Slash:        return ExprOp::Divide;
        case TokenKind::Percent:      return ExprOp::Modulo;
        case TokenKind::Caret:        return ExprOp::Power;
        case TokenKind::Less:         return ExprOp::Less;
        case TokenKind::LessEqual:    return ExprOp::LessEqual;
        case TokenKind::Equal:        return ExprOp::Equal;
        case TokenKind::NotEqual:    return ExprOp::NotEqual;
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
    ExprPtr left = ParseTernary();
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
    Fail(m_token, "syntax error");
}

ExprPtr Parser::ParseTernary() {
    ExprPtr condition = ParseOr();
    if (m_token.kind != TokenKind::Question) {
        return condition;
    }
    Advance();
    ExprPtr ifTrue = ParseTernary();
    if (m_token.kind != TokenKind::Colon) {
        Fail(m_token, "syntax error");
    }
    Advance();
    ExprPtr ifFalse = ParseTernary();
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
        left = MakeBinary(ExprOp::Or, std::move(left), ParseAnd());
    }
    return left;
}

ExprPtr Parser::ParseAnd() {
    ExprPtr left = ParseIn();
    while (m_token.kind == TokenKind::And) {
        Advance();
        left = MakeBinary(ExprOp::And, std::move(left), ParseIn());
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
        Advance();
        ExprPtr expr = std::make_unique<Expr>();
        expr->kind = ExprKind::In;
        expr->position = left->position;
        expr->text = arrayName;
        expr->operands.push_back(std::move(left));
        left = std::move(expr);
    }
    return left;
}

ExprPtr Parser::ParseMatch() {
    ExprPtr left = ParseComparison();
    while (m_token.kind == TokenKind::Tilde || m_token.kind == TokenKind::NoMatch) {
        const ExprOp op = m_token.kind == TokenKind::Tilde ? ExprOp::Match : ExprOp::NoMatch;
        Advance();
        left = MakeBinary(op, std::move(left), ParseComparison());
    }
    return left;
}

ExprPtr Parser::ParseComparison() {
    ExprPtr left = ParseGetlineCommand();
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
    return MakeBinary(op, std::move(left), ParseGetlineCommand());
}

ExprPtr Parser::ParseGetlineCommand() {
    ExprPtr command = ParseConcatenation();
    if (m_token.kind != TokenKind::Pipe) {
        return command;
    }
    Advance();
    if (m_token.kind != TokenKind::Getline) {
        Fail(m_token, "syntax error");
    }
    const Token keyword = m_token;
    Advance();
    ExprPtr expr = std::make_unique<Expr>();
    expr->kind = ExprKind::Getline;
    expr->position = command->position;
    expr->getlineForm = GetlineForm::Command;
    expr->operands.push_back(std::move(command));
    expr->target = ParseGetlineTarget();
    return expr;
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
    if (m_token.kind == TokenKind::Increment || m_token.kind == TokenKind::Decrement) {
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
    std::vector<ExprPtr> arguments;
    if (m_token.kind == TokenKind::RightParen) {
        return arguments;
    }
    arguments.push_back(ParseExpression());
    while (m_token.kind == TokenKind::Comma) {
        Advance();
        arguments.push_back(ParseExpression());
    }
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
        const Token name = m_token;
        Advance();
        if (m_token.kind != TokenKind::LeftBracket) {
            ExprPtr expr = MakeExpr(ExprKind::Variable, name);
            expr->text = name.text;
            return expr;
        }
        Advance();
        std::vector<ExprPtr> subscripts;
        subscripts.push_back(ParseExpression());
        while (m_token.kind == TokenKind::Comma) {
            Advance();
            subscripts.push_back(ParseExpression());
        }
        if (m_token.kind != TokenKind::RightBracket) {
            Fail(m_token, "syntax error");
        }
        Advance();
        ExprPtr expr = MakeExpr(ExprKind::Index, name);
        expr->text = name.text;
        expr->operands = std::move(subscripts);
        return expr;
    }
    if (m_token.kind == TokenKind::Dollar) {
        const Token dollar = m_token;
        Advance();
        ExprPtr expr = MakeExpr(ExprKind::Field, dollar);
        expr->operands.push_back(ParseDollarOperand());
        return expr;
    }
    return nullptr;
}

ExprPtr Parser::ParsePrimary() {
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
            if (exprs.size() == 1) {
                return std::move(exprs[0]);  // a grouping keeps no node
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
            Advance();
            ExprPtr expr = std::make_unique<Expr>();
            expr->kind = ExprKind::In;
            expr->position = SourcePosition{m_sourceIndex, open.line};
            expr->text = arrayName;
            expr->operands = std::move(exprs);
            return expr;
        }
        case TokenKind::Name: {
            const Token name = m_token;
            Advance();
            if (m_token.kind != TokenKind::LeftBracket) {
                ExprPtr expr = MakeExpr(ExprKind::Variable, name);
                expr->text = name.text;
                return expr;
            }
            Advance();
            std::vector<ExprPtr> subscripts;
            subscripts.push_back(ParseExpression());
            while (m_token.kind == TokenKind::Comma) {
                Advance();
                subscripts.push_back(ParseExpression());
            }
            if (m_token.kind != TokenKind::RightBracket) {
                Fail(m_token, "syntax error");
            }
            Advance();
            ExprPtr expr = MakeExpr(ExprKind::Index, name);
            expr->text = name.text;
            expr->operands = std::move(subscripts);
            return expr;
        }
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
        case TokenKind::Dollar: {
            const Token dollar = m_token;
            Advance();
            ExprPtr expr = MakeExpr(ExprKind::Field, dollar);
            expr->operands.push_back(ParseDollarOperand());
            return expr;
        }
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