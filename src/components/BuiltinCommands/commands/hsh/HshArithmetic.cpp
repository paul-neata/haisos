#include "commands/hsh/HshArithmetic.h"

#include <cctype>
#include <cinttypes>
#include <cstdlib>
#include <string_view>
#include <vector>

#include "commands/hsh/HshError.h"

namespace Haisos::Hsh {

namespace {

// All arithmetic is done on uintmax_t and turned back, so overflow, negation
// of INTMAX_MIN and friends are defined (wrapping two's complement), never UB.
uintmax_t U(intmax_t v) { return static_cast<uintmax_t>(v); }
intmax_t S(uintmax_t v) { return static_cast<intmax_t>(v); }

intmax_t Add(intmax_t a, intmax_t b) { return S(U(a) + U(b)); }
intmax_t Sub(intmax_t a, intmax_t b) { return S(U(a) - U(b)); }
intmax_t Mul(intmax_t a, intmax_t b) { return S(U(a) * U(b)); }
intmax_t Neg(intmax_t a) { return S(0 - U(a)); }
intmax_t BitNot(intmax_t a) { return S(~U(a)); }

// Shift counts modulo 64, as dash does on x86-64; the right shift is arithmetic.
intmax_t ShiftLeft(intmax_t a, intmax_t b) { return S(U(a) << (U(b) & 63)); }
intmax_t ShiftRight(intmax_t a, intmax_t b) {
    uintmax_t count = U(b) & 63;
    if (count == 0) return a;
    uintmax_t r = U(a) >> count;
    if (a < 0) r |= ~uintmax_t{0} << (64 - count);
    return S(r);
}

// Truncating. INTMAX_MIN / -1 overflows the CPU (dash crashes there); hsh does not.
intmax_t Div(intmax_t a, intmax_t b) {
    if (a == INTMAX_MIN && b == -1) return INTMAX_MIN;
    return a / b;
}
intmax_t Mod(intmax_t a, intmax_t b) {
    if (a == INTMAX_MIN && b == -1) return 0;
    return a % b;
}

enum class TokenKind {
    End, Invalid, Number, Name,
    Plus, Minus, Star, Slash, Percent,
    Shl, Shr, Lt, Le, Gt, Ge, EqEq, NotEq,
    And, Xor, Or, Not, Tilde,
    AndAnd, OrOr, Question, Colon,
    Assign, PlusEq, MinusEq, StarEq, SlashEq, PercentEq, ShlEq, ShrEq, AndEq, XorEq, OrEq,
    LParen, RParen
};

struct Token {
    TokenKind kind = TokenKind::End;
    intmax_t number = 0;
    std::string name;
};

bool IsBlank(char c) { return c == ' ' || c == '\t' || c == '\n'; }

// dash's tokens: blanks skipped; a number is exactly what strtoimax(0) reads
// from a digit on (decimal, octal, hex; saturating on overflow -- so "08" is
// 0 then 8); a name [A-Za-z_][A-Za-z0-9_]*; operators longest first; anything
// else one Invalid token (there is no ++, --, ** or comma).
std::vector<Token> Tokenize(const std::string& expression) {
    static const std::pair<std::string_view, TokenKind> kTwoChar[] = {
        {"<<", TokenKind::Shl}, {">>", TokenKind::Shr}, {"<=", TokenKind::Le},
        {">=", TokenKind::Ge}, {"==", TokenKind::EqEq}, {"!=", TokenKind::NotEq},
        {"&&", TokenKind::AndAnd}, {"||", TokenKind::OrOr},
        {"*=", TokenKind::StarEq}, {"/=", TokenKind::SlashEq}, {"%=", TokenKind::PercentEq},
        {"+=", TokenKind::PlusEq}, {"-=", TokenKind::MinusEq},
        {"&=", TokenKind::AndEq}, {"^=", TokenKind::XorEq}, {"|=", TokenKind::OrEq},
    };
    std::vector<Token> tokens;
    size_t i = 0;
    while (i < expression.size()) {
        char c = expression[i];
        if (IsBlank(c)) { ++i; continue; }
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            char* end = nullptr;
            intmax_t value = std::strtoimax(expression.c_str() + i, &end, 0);
            i = static_cast<size_t>(end - expression.c_str());
            tokens.push_back(Token{TokenKind::Number, value, {}});
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_') {
            size_t begin = i;
            while (i < expression.size() &&
                   (std::isalnum(static_cast<unsigned char>(expression[i])) != 0 || expression[i] == '_'))
                ++i;
            tokens.push_back(Token{TokenKind::Name, 0, expression.substr(begin, i - begin)});
            continue;
        }
        std::string_view rest(expression.data() + i, expression.size() - i);
        if (rest.substr(0, 3) == "<<=") { tokens.push_back(Token{TokenKind::ShlEq}); i += 3; continue; }
        if (rest.substr(0, 3) == ">>=") { tokens.push_back(Token{TokenKind::ShrEq}); i += 3; continue; }
        bool two = false;
        for (const auto& [text, kind] : kTwoChar) {
            if (rest.substr(0, 2) == text) { tokens.push_back(Token{kind}); i += 2; two = true; break; }
        }
        if (two) continue;
        switch (c) {
            case '+': tokens.push_back(Token{TokenKind::Plus}); break;
            case '-': tokens.push_back(Token{TokenKind::Minus}); break;
            case '*': tokens.push_back(Token{TokenKind::Star}); break;
            case '/': tokens.push_back(Token{TokenKind::Slash}); break;
            case '%': tokens.push_back(Token{TokenKind::Percent}); break;
            case '<': tokens.push_back(Token{TokenKind::Lt}); break;
            case '>': tokens.push_back(Token{TokenKind::Gt}); break;
            case '&': tokens.push_back(Token{TokenKind::And}); break;
            case '^': tokens.push_back(Token{TokenKind::Xor}); break;
            case '|': tokens.push_back(Token{TokenKind::Or}); break;
            case '!': tokens.push_back(Token{TokenKind::Not}); break;
            case '~': tokens.push_back(Token{TokenKind::Tilde}); break;
            case '?': tokens.push_back(Token{TokenKind::Question}); break;
            case ':': tokens.push_back(Token{TokenKind::Colon}); break;
            case '=': tokens.push_back(Token{TokenKind::Assign}); break;
            case '(': tokens.push_back(Token{TokenKind::LParen}); break;
            case ')': tokens.push_back(Token{TokenKind::RParen}); break;
            default:  tokens.push_back(Token{TokenKind::Invalid}); break;
        }
        ++i;
    }
    tokens.push_back(Token{TokenKind::End});
    return tokens;
}

class ArithmeticParser {
public:
    ArithmeticParser(const std::string& expression, IArithmeticVariables& variables)
        : m_expression(expression), m_variables(variables), m_tokens(Tokenize(expression)) {}

    intmax_t Run() {
        intmax_t value = ParseAssign().value;
        if (Peek() != TokenKind::End) Fail("expecting EOF");
        return value;
    }

private:
    // A value plus, when it is a plain variable read, its name: assignment is
    // only allowed directly after a name.
    struct Value {
        intmax_t value = 0;
        std::string lvalue;
    };

    // Runs f() with evaluation on or off: off is the branch &&, || or ?:
    // does not take -- still parsed (syntax errors stand), never evaluated
    // (nothing is looked up, assigned or divided).
    template <typename F>
    intmax_t WithEvaluation(bool evaluate, F&& f) {
        bool saved = m_evaluating;
        m_evaluating = evaluate;
        intmax_t result = 0;
        try {
            result = f();
        } catch (...) {
            m_evaluating = saved;
            throw;
        }
        m_evaluating = saved;
        return result;
    }

    TokenKind Peek() const { return m_tokens[m_pos].kind; }
    const Token& Next() { return m_tokens[m_pos++]; }

    [[noreturn]] void Fail(const char* what) const {
        throw ShellError("arithmetic expression: " + std::string(what) + ": \"" + m_expression + "\"");
    }

    Value ParseAssign() {
        Value lhs = ParseConditional();
        if (!IsAssignOp(Peek()) || lhs.lvalue.empty()) return lhs;
        TokenKind op = Next().kind;
        std::string name = lhs.lvalue;
        Value rhs = ParseAssign(); // right associative
        if (!m_evaluating) { lhs.value = 0; lhs.lvalue.clear(); return lhs; }
        intmax_t result;
        switch (op) {
            case TokenKind::Assign:    result = rhs.value; break;
            case TokenKind::PlusEq:    result = Add(lhs.value, rhs.value); break;
            case TokenKind::MinusEq:   result = Sub(lhs.value, rhs.value); break;
            case TokenKind::StarEq:    result = Mul(lhs.value, rhs.value); break;
            case TokenKind::SlashEq:   if (rhs.value == 0) Fail("division by zero"); result = Div(lhs.value, rhs.value); break;
            case TokenKind::PercentEq: if (rhs.value == 0) Fail("division by zero"); result = Mod(lhs.value, rhs.value); break;
            case TokenKind::ShlEq:     result = ShiftLeft(lhs.value, rhs.value); break;
            case TokenKind::ShrEq:     result = ShiftRight(lhs.value, rhs.value); break;
            case TokenKind::AndEq:     result = S(U(lhs.value) & U(rhs.value)); break;
            case TokenKind::XorEq:     result = S(U(lhs.value) ^ U(rhs.value)); break;
            case TokenKind::OrEq:      result = S(U(lhs.value) | U(rhs.value)); break;
            default: result = rhs.value; break;
        }
        if (!m_variables.Set(name, std::to_string(result)))
            throw ShellError(name + ": is read only");
        lhs.value = result;
        lhs.lvalue.clear();
        return lhs;
    }

    Value ParseConditional() {
        Value condition = ParseLogicalOr();
        if (Peek() != TokenKind::Question) return condition;
        Next();
        bool thenEvaluates = m_evaluating && condition.value != 0;
        bool elseEvaluates = m_evaluating && condition.value == 0;
        intmax_t thenValue = WithEvaluation(thenEvaluates,
            [&] { return ParseAssign().value; });
        if (Peek() != TokenKind::Colon) Fail("expecting ':'");
        Next();
        intmax_t elseValue = WithEvaluation(elseEvaluates,
            [&] { return ParseAssign().value; }); // right associative
        Value result;
        if (m_evaluating) result.value = condition.value != 0 ? thenValue : elseValue;
        return result;
    }

    Value ParseLogicalOr() {
        Value lhs = ParseLogicalAnd();
        while (Peek() == TokenKind::OrOr) {
            Next();
            intmax_t rhs = WithEvaluation(m_evaluating && lhs.value == 0,
                [&] { return ParseLogicalAnd().value; });
            lhs.value = m_evaluating && (lhs.value != 0 || rhs != 0) ? 1 : 0;
            lhs.lvalue.clear();
        }
        return lhs;
    }

    Value ParseLogicalAnd() {
        Value lhs = ParseBitOr();
        while (Peek() == TokenKind::AndAnd) {
            Next();
            intmax_t rhs = WithEvaluation(m_evaluating && lhs.value != 0,
                [&] { return ParseBitOr().value; });
            lhs.value = m_evaluating && (lhs.value != 0 && rhs != 0) ? 1 : 0;
            lhs.lvalue.clear();
        }
        return lhs;
    }

    Value ParseBitOr()  { return ParseBinaryBits(&ArithmeticParser::ParseBitXor, TokenKind::Or); }
    Value ParseBitXor() { return ParseBinaryBits(&ArithmeticParser::ParseBitAnd, TokenKind::Xor); }
    Value ParseBitAnd() { return ParseBinaryBits(&ArithmeticParser::ParseEquality, TokenKind::And); }

    Value ParseBinaryBits(Value (ArithmeticParser::*next)(), TokenKind op) {
        Value lhs = (this->*next)();
        while (Peek() == op) {
            Next();
            Value rhs = (this->*next)();
            if (m_evaluating) {
                if (op == TokenKind::Or) lhs.value = S(U(lhs.value) | U(rhs.value));
                else if (op == TokenKind::Xor) lhs.value = S(U(lhs.value) ^ U(rhs.value));
                else lhs.value = S(U(lhs.value) & U(rhs.value));
            } else lhs.value = 0;
            lhs.lvalue.clear();
        }
        return lhs;
    }

    Value ParseEquality() {
        Value lhs = ParseRelational();
        while (Peek() == TokenKind::EqEq || Peek() == TokenKind::NotEq) {
            bool equal = Next().kind == TokenKind::EqEq;
            Value rhs = ParseRelational();
            if (m_evaluating) lhs.value = (lhs.value == rhs.value) == equal ? 1 : 0;
            else lhs.value = 0;
            lhs.lvalue.clear();
        }
        return lhs;
    }

    Value ParseRelational() {
        Value lhs = ParseShift();
        while (Peek() == TokenKind::Lt || Peek() == TokenKind::Le ||
               Peek() == TokenKind::Gt || Peek() == TokenKind::Ge) {
            TokenKind op = Next().kind;
            Value rhs = ParseShift();
            if (m_evaluating) {
                switch (op) {
                    case TokenKind::Lt: lhs.value = lhs.value < rhs.value ? 1 : 0; break;
                    case TokenKind::Le: lhs.value = lhs.value <= rhs.value ? 1 : 0; break;
                    case TokenKind::Gt: lhs.value = lhs.value > rhs.value ? 1 : 0; break;
                    default:            lhs.value = lhs.value >= rhs.value ? 1 : 0; break;
                }
            } else lhs.value = 0;
            lhs.lvalue.clear();
        }
        return lhs;
    }

    Value ParseShift() {
        Value lhs = ParseAdditive();
        while (Peek() == TokenKind::Shl || Peek() == TokenKind::Shr) {
            TokenKind op = Next().kind;
            Value rhs = ParseAdditive();
            if (m_evaluating)
                lhs.value = op == TokenKind::Shl ? ShiftLeft(lhs.value, rhs.value)
                                                 : ShiftRight(lhs.value, rhs.value);
            else lhs.value = 0;
            lhs.lvalue.clear();
        }
        return lhs;
    }

    Value ParseAdditive() {
        Value lhs = ParseMultiplicative();
        while (Peek() == TokenKind::Plus || Peek() == TokenKind::Minus) {
            TokenKind op = Next().kind;
            Value rhs = ParseMultiplicative();
            if (m_evaluating)
                lhs.value = op == TokenKind::Plus ? Add(lhs.value, rhs.value) : Sub(lhs.value, rhs.value);
            else lhs.value = 0;
            lhs.lvalue.clear();
        }
        return lhs;
    }

    Value ParseMultiplicative() {
        Value lhs = ParseUnary();
        while (Peek() == TokenKind::Star || Peek() == TokenKind::Slash || Peek() == TokenKind::Percent) {
            TokenKind op = Next().kind;
            Value rhs = ParseUnary();
            if (m_evaluating) {
                if ((op == TokenKind::Slash || op == TokenKind::Percent) && rhs.value == 0)
                    Fail("division by zero");
                if (op == TokenKind::Star) lhs.value = Mul(lhs.value, rhs.value);
                else if (op == TokenKind::Slash) lhs.value = Div(lhs.value, rhs.value);
                else lhs.value = Mod(lhs.value, rhs.value);
            } else lhs.value = 0;
            lhs.lvalue.clear();
        }
        return lhs;
    }

    Value ParseUnary() {
        switch (Peek()) {
            case TokenKind::Plus:  Next(); return ParseUnary();
            case TokenKind::Minus: Next(); return Mapped(ParseUnary(), [](intmax_t v) { return Neg(v); });
            case TokenKind::Tilde: Next(); return Mapped(ParseUnary(), [](intmax_t v) { return BitNot(v); });
            case TokenKind::Not:   Next(); return Mapped(ParseUnary(), [](intmax_t v) { return v == 0 ? 1 : 0; });
            default: return ParsePrimary();
        }
    }

    template <typename F>
    Value Mapped(Value v, F&& f) {
        if (m_evaluating) v.value = f(v.value);
        else v.value = 0;
        v.lvalue.clear();
        return v;
    }

    Value ParsePrimary() {
        switch (Peek()) {
            case TokenKind::Number: {
                Value v;
                v.value = Next().number;
                return v;
            }
            case TokenKind::Name: {
                const Token& token = Next();
                Value v;
                v.lvalue = token.name;
                v.value = VariableValue(token.name);
                return v;
            }
            case TokenKind::LParen: {
                Next();
                Value v = ParseAssign();
                if (Peek() != TokenKind::RParen) Fail("expecting ')'");
                Next();
                v.lvalue.clear();
                return v;
            }
            default: Fail("expecting primary");
        }
    }

    // Unset or empty is 0; otherwise the value must read, after optional
    // leading blanks, as strtoimax(0) (sign allowed) followed by blanks only.
    intmax_t VariableValue(const std::string& name) {
        if (!m_evaluating) return 0;
        std::optional<std::string> stored = m_variables.Get(name);
        if (!stored.has_value() || stored->empty()) return 0;
        const char* text = stored->c_str();
        char* end = nullptr;
        intmax_t value = std::strtoimax(text, &end, 0);
        if (end == text) throw ShellError("Illegal number: " + *stored);
        while (*end != '\0' && IsBlank(*end)) ++end;
        if (*end != '\0') throw ShellError("Illegal number: " + *stored);
        return value;
    }

    static bool IsAssignOp(TokenKind kind) {
        switch (kind) {
            case TokenKind::Assign: case TokenKind::PlusEq: case TokenKind::MinusEq:
            case TokenKind::StarEq: case TokenKind::SlashEq: case TokenKind::PercentEq:
            case TokenKind::ShlEq: case TokenKind::ShrEq:
            case TokenKind::AndEq: case TokenKind::XorEq: case TokenKind::OrEq:
                return true;
            default: return false;
        }
    }

    const std::string& m_expression;
    IArithmeticVariables& m_variables;
    std::vector<Token> m_tokens;
    size_t m_pos = 0;
    bool m_evaluating = true;
};

} // namespace

intmax_t EvaluateArithmetic(const std::string& expression, IArithmeticVariables& variables) {
    return ArithmeticParser(expression, variables).Run();
}

} // namespace Haisos::Hsh
