#include "commands/jq/JqLexer.h"

namespace Haisos::Jq {
namespace {

bool IsNameStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool IsNameChar(char c) {
    return IsNameStart(c) || (c >= '0' && c <= '9');
}

bool IsKeyword(const std::string& word) {
    return word == "as" || word == "def" || word == "if" || word == "then" ||
           word == "elif" || word == "else" || word == "end" ||
           word == "and" || word == "or" || word == "reduce" ||
           word == "foreach" || word == "try" || word == "catch" ||
           word == "label" || word == "break" || word == "import" ||
           word == "include" || word == "module" || word == "__loc__";
}

// Appends |codePoint| to |out| as UTF-8.
void AppendUtf8(std::string& out, unsigned int codePoint) {
    if (codePoint < 0x80) {
        out += static_cast<char>(codePoint);
    } else if (codePoint < 0x800) {
        out += static_cast<char>(0xC0 | (codePoint >> 6));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    } else if (codePoint < 0x10000) {
        out += static_cast<char>(0xE0 | (codePoint >> 12));
        out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (codePoint >> 18));
        out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
}

int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

} // namespace

std::string TokenNameInMessage(const Token& token) {
    switch (token.type) {
        case TokenType::End: return "end of file";
        case TokenType::Invalid: return "INVALID_CHARACTER";
        case TokenType::Identifier: return "IDENT";
        case TokenType::Field: return "FIELD";
        case TokenType::Variable: return "BINDING";
        case TokenType::LocVariable: return "$__loc__";
        case TokenType::Number: return "LITERAL";
        case TokenType::Format: return "FORMAT";
        case TokenType::StringStart: return "QQSTRING_START";
        case TokenType::StringText: return "QQSTRING_TEXT";
        case TokenType::InterpolationStart: return "QQSTRING_INTERP_START";
        case TokenType::InterpolationEnd: return "QQSTRING_INTERP_END";
        case TokenType::StringEnd: return "QQSTRING_END";
        // A keyword or operator is named by its spelling, unquoted.
        case TokenType::Keyword:
        case TokenType::Operator: return token.text;
        case TokenType::Char: return "'" + token.text + "'";
    }
    return token.text;
}

Lexer::Lexer(std::string_view program) : m_program(program) {}

Token Lexer::Next() {
    m_error.clear();
    if (m_inString)
        return LexInString();

    // Whitespace and comments: a comment runs from '#' to the end of the
    // line and no further.
    while (!AtEnd()) {
        char c = m_program[m_pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++m_pos;
        } else if (c == '#') {
            while (!AtEnd() && m_program[m_pos] != '\n')
                ++m_pos;
        } else {
            break;
        }
    }

    const size_t start = m_pos;
    if (AtEnd()) {
        Token t;  // End
        t.begin = m_pos;
        t.end = m_pos;
        return t;
    }
    const char c = m_program[m_pos];

    // The multi-character operators, longest first: ?// and //= run three,
    // the rest two.
    if (c == '?' && m_pos + 2 < m_program.size() &&
        m_program[m_pos + 1] == '/' && m_program[m_pos + 2] == '/')
        return MakeOperator(start, 3);
    if (c == '/' && m_pos + 2 < m_program.size() &&
        m_program[m_pos + 1] == '/' && m_program[m_pos + 2] == '=')
        return MakeOperator(start, 3);
    for (const char* op : {"!=", "==", "//", "|=", "+=", "-=", "*=",
                           "/=", "%=", "<=", ">=", ".."}) {
        if (c == op[0] && m_pos + 1 < m_program.size() &&
            m_program[m_pos + 1] == op[1])
            return MakeOperator(start, 2);
    }

    // '.' leads '..' (above), a number (.5) and a field (.a); in every other
    // place it is the character itself.
    if (c == '.' && m_pos + 1 < m_program.size()) {
        if (IsDigit(m_program[m_pos + 1]))
            return LexNumber();
        if (IsNameStart(m_program[m_pos + 1]))
            return LexField();
    }
    if (IsNameStart(c))
        return LexIdent();
    if (IsDigit(c))
        return LexNumber();
    if (c == '$' && m_pos + 1 < m_program.size() &&
        IsNameStart(m_program[m_pos + 1]))
        return LexIdent();
    if (c == '@')
        return LexFormat();
    if (c == '"')
        return LexStringStart();

    // Brackets: an opener is pushed, a matching closer pops the innermost
    // open one; anything else a closer might match is Invalid. The ')' of a
    // '\(' interpolation returns to the string instead.
    if (c == '(' || c == '[' || c == '{') {
        PushBracket(c == '(' ? Bracket::Paren
                    : c == '[' ? Bracket::Square
                               : Bracket::Brace);
        return MakeChar(start, 1);
    }
    if (c == ')' || c == ']' || c == '}') {
        Bracket popped;
        if (!CloseBracket(c, popped)) {
            if (c == ')' && popped == Bracket::Interp) {
                ++m_pos;  // the ')' is the interpolation's, not string text
                m_inString = true;
                Token t;
                t.type = TokenType::InterpolationEnd;
                t.text = ")";
                t.begin = start;
                t.end = start + 1;
                return t;
            }
            return MakeChar(start, 1);
        }
        return MakeInvalid(start, 1);
    }

    switch (c) {
        case '.': case '?': case '=': case ';': case ',': case ':':
        case '|': case '+': case '-': case '*': case '/': case '%':
        case '$': case '<': case '>':
            return MakeChar(start, 1);
        default:
            return MakeInvalid(start, 1);
    }
}

Token Lexer::LexIdent() {
    const size_t start = m_pos;
    std::string name;
    bool variable = m_program[m_pos] == '$';
    if (variable)
        ++m_pos;
    for (;;) {
        while (!AtEnd() && IsNameChar(m_program[m_pos]))
            name += m_program[m_pos++];
        // a::b is one name.
        if (m_pos + 1 < m_program.size() && m_program[m_pos] == ':' &&
            m_program[m_pos + 1] == ':' && m_pos + 2 < m_program.size() &&
            IsNameStart(m_program[m_pos + 2])) {
            name += "::";
            m_pos += 2;
        } else {
            break;
        }
    }
    Token t;
    if (variable) {
        if (name == "__loc__") {
            t.type = TokenType::LocVariable;
            t.text = name;
        } else {
            t.type = TokenType::Variable;
            t.text = name;
        }
    } else if (IsKeyword(name)) {
        t.type = TokenType::Keyword;
        t.text = name;
    } else {
        t.type = TokenType::Identifier;
        t.text = name;
    }
    t.begin = start;
    t.end = m_pos;
    return t;
}

Token Lexer::LexNumber() {
    const size_t start = m_pos;
    if (m_program[m_pos] == '.')
        ++m_pos;
    while (!AtEnd() && IsDigit(m_program[m_pos]))
        ++m_pos;
    if (m_pos < m_program.size() && m_program[m_pos] == '.') {
        ++m_pos;
        while (!AtEnd() && IsDigit(m_program[m_pos]))
            ++m_pos;
    }
    if (m_pos < m_program.size() &&
        (m_program[m_pos] == 'e' || m_program[m_pos] == 'E')) {
        const size_t e = m_pos;
        ++m_pos;
        if (m_pos < m_program.size() &&
            (m_program[m_pos] == '+' || m_program[m_pos] == '-'))
            ++m_pos;
        if (!AtEnd() && IsDigit(m_program[m_pos])) {
            while (!AtEnd() && IsDigit(m_program[m_pos]))
                ++m_pos;
        } else {
            m_pos = e;  // no exponent: the 'e' is not part of the number
        }
    }
    Token t;
    t.type = TokenType::Number;
    t.text = std::string(m_program.substr(start, m_pos - start));
    t.begin = start;
    t.end = m_pos;
    return t;
}

Token Lexer::LexField() {
    const size_t start = m_pos;
    ++m_pos;  // the '.'
    const size_t nameStart = m_pos;
    while (!AtEnd() && IsNameChar(m_program[m_pos]))
        ++m_pos;
    Token t;
    t.type = TokenType::Field;
    t.text = std::string(m_program.substr(nameStart, m_pos - nameStart));
    t.begin = start;
    t.end = m_pos;
    return t;
}

Token Lexer::LexFormat() {
    const size_t start = m_pos;
    ++m_pos;  // the '@'
    const size_t nameStart = m_pos;
    while (!AtEnd() && IsNameChar(m_program[m_pos]))
        ++m_pos;
    Token t;
    if (m_pos == nameStart) {
        t.type = TokenType::Invalid;
    } else {
        t.type = TokenType::Format;
        t.text = std::string(m_program.substr(nameStart, m_pos - nameStart));
    }
    t.begin = start;
    t.end = m_pos;
    return t;
}

Token Lexer::LexStringStart() {
    ++m_pos;
    m_inString = true;
    Token t;
    t.type = TokenType::StringStart;
    t.text = "\"";
    t.begin = m_pos - 1;
    t.end = m_pos;
    return t;
}

Token Lexer::MakeOperator(size_t begin, size_t length) {
    m_pos = begin + length;
    Token t;
    t.type = TokenType::Operator;
    t.text = std::string(m_program.substr(begin, length));
    t.begin = begin;
    t.end = m_pos;
    return t;
}

Token Lexer::MakeChar(size_t begin, size_t length) {
    m_pos = begin + length;
    Token t;
    t.type = TokenType::Char;
    t.text = std::string(m_program.substr(begin, length));
    t.begin = begin;
    t.end = m_pos;
    return t;
}

Token Lexer::MakeInvalid(size_t begin, size_t length) {
    m_pos = begin + length;
    Token t;
    t.type = TokenType::Invalid;
    t.text = std::string(m_program.substr(begin, length));
    t.begin = begin;
    t.end = m_pos;
    return t;
}

bool Lexer::CloseBracket(char closing, Bracket& popped) {
    if (m_brackets.empty()) {
        popped = Bracket::Paren;
        return true;
    }
    // A '\(' interpolation closes only with ')'.
    if (m_brackets.back() == Bracket::Interp) {
        if (closing != ')')
            return true;
        popped = m_brackets.back();
        m_brackets.pop_back();
        return false;
    }
    const Bracket wanted = closing == ')' ? Bracket::Paren
                          : closing == ']' ? Bracket::Square
                                           : Bracket::Brace;
    if (m_brackets.back() != wanted)
        return true;
    popped = m_brackets.back();
    m_brackets.pop_back();
    return false;
}

char Lexer::Peek(size_t ahead) const {
    return m_pos + ahead < m_program.size() ? m_program[m_pos + ahead] : '\0';
}

Token Lexer::LexInString() {
    const size_t start = m_pos;
    if (AtEnd()) {
        Token t;  // End
        t.begin = m_pos;
        t.end = m_pos;
        return t;
    }
    if (m_program[m_pos] == '"') {
        ++m_pos;
        m_inString = false;
        Token t;
        t.type = TokenType::StringEnd;
        t.text = "\"";
        t.begin = start;
        t.end = m_pos;
        return t;
    }
    // '\(' opens an interpolation and is not an escape.
    if (m_program[m_pos] == '\\' && Peek(1) == '(') {
        m_pos += 2;
        m_inString = false;
        PushBracket(Bracket::Interp);
        Token t;
        t.type = TokenType::InterpolationStart;
        t.text = "\\(";
        t.begin = start;
        t.end = m_pos;
        return t;
    }

    // A run of text and escapes, up to the closing quote, an interpolation
    // or the end of the input.
    std::string text;
    while (!AtEnd()) {
        const char c = m_program[m_pos];
        if (c == '"' || (c == '\\' && Peek(1) == '('))
            break;
        if (c == '\\') {
            if (!ScanEscapeRun(text))
                return MakeInvalid(m_errorOffset, m_pos - m_errorOffset);
            continue;
        }
        text += c;
        ++m_pos;
    }
    Token t;
    t.type = TokenType::StringText;
    t.text = text;
    t.begin = start;
    t.end = m_pos;
    return t;
}

// Scans the escape run at m_pos: its first '\', then every escape that
// directly follows it ('\u' takes up to four following bytes, stopping early
// at a '\' or a '"'), appending what the escapes decode to. The whole run is
// scanned even after a bad escape: jq's message holds the whole run. Returns
// false when the run holds a bad escape, with m_error holding the message.
bool Lexer::ScanEscapeRun(std::string& text) {
    const size_t runStart = m_pos;
    size_t pos = m_pos;
    std::string decoded;
    std::string firstProblem;

    auto hexAt = [&](size_t at) { return HexValue(m_program[at]); };

    while (pos < m_program.size() && m_program[pos] == '\\') {
        // '\(' ends the run: it is an interpolation, not an escape.
        if (pos + 1 < m_program.size() && m_program[pos + 1] == '(')
            break;
        if (pos + 1 >= m_program.size()) {
            if (firstProblem.empty())
                firstProblem = "Invalid escape";
            pos = pos + 1;
            continue;
        }
        const char e = m_program[pos + 1];
        if (e == '"' || e == '\\' || e == '/' || e == 'b' || e == 'f' ||
            e == 'n' || e == 'r' || e == 't') {
            switch (e) {
                case '"': decoded += '"'; break;
                case '\\': decoded += '\\'; break;
                case '/': decoded += '/'; break;
                case 'b': decoded += '\b'; break;
                case 'f': decoded += '\f'; break;
                case 'n': decoded += '\n'; break;
                case 'r': decoded += '\r'; break;
                case 't': decoded += '\t'; break;
            }
            pos += 2;
            continue;
        }
        if (e == 'u') {
            // Up to four following bytes, stopping early at a '\' or a '"'.
            size_t hexStart = pos + 2;
            size_t hexEnd = hexStart;
            int digits = 0;
            while (hexEnd < m_program.size() && digits < 4 &&
                   m_program[hexEnd] != '\\' && m_program[hexEnd] != '"') {
                ++hexEnd;
                ++digits;
            }
            bool allHex = true;
            unsigned int value = 0;
            for (size_t i = hexStart; i < hexEnd; ++i) {
                const int h = hexAt(i);
                if (h < 0)
                    allHex = false;
                else
                    value = value * 16 + static_cast<unsigned int>(h);
            }
            if (digits < 4 || !allHex) {
                if (firstProblem.empty())
                    firstProblem = digits < 4
                        ? "Invalid \\uXXXX escape"
                        : "Invalid characters in \\uXXXX escape";
                pos = hexEnd;
                continue;
            }
            pos = hexEnd;
            if (value >= 0xD800 && value <= 0xDBFF) {
                // A high surrogate must be followed by a low one, here in
                // the run.
                unsigned int low = 0;
                bool paired = false;
                if (pos + 1 < m_program.size() && m_program[pos] == '\\' &&
                    m_program[pos + 1] == 'u') {
                    size_t lowStart = pos + 2;
                    unsigned int v = 0;
                    bool ok = true;
                    for (int i = 0; i < 4; ++i) {
                        if (lowStart + static_cast<size_t>(i) >=
                            m_program.size()) {
                            ok = false;
                            break;
                        }
                        const int h = hexAt(lowStart + static_cast<size_t>(i));
                        if (h < 0) {
                            ok = false;
                            break;
                        }
                        v = v * 16 + static_cast<unsigned int>(h);
                    }
                    if (ok && v >= 0xDC00 && v <= 0xDFFF) {
                        low = v;
                        paired = true;
                    }
                }
                if (!paired) {
                    if (firstProblem.empty())
                        firstProblem =
                            "Invalid \\uXXXX\\uXXXX surrogate pair escape";
                } else {
                    const unsigned int codePoint =
                        0x10000 + ((value - 0xD800) << 10) + (low - 0xDC00);
                    AppendUtf8(decoded, codePoint);
                    pos = pos + 6;
                }
                continue;
            }
            if (value >= 0xDC00 && value <= 0xDFFF) {
                // A low surrogate on its own becomes U+FFFD.
                AppendUtf8(decoded, 0xFFFD);
            } else {
                AppendUtf8(decoded, value);
            }
            continue;
        }
        if (firstProblem.empty())
            firstProblem = "Invalid escape";
        pos = pos + 2;
    }

    if (!firstProblem.empty()) {
        const size_t runLength = pos - runStart;
        std::string quoted = "\"";
        quoted.append(m_program.substr(runStart, runLength));
        quoted += "\"";
        m_error = firstProblem + " at line 1, column " +
                  std::to_string(runLength + 2) + " (while parsing '" +
                  quoted + "')";
        m_errorOffset = runStart;
        m_pos = pos;
        return false;
    }
    text += decoded;
    m_pos = pos;
    return true;
}

} // namespace Haisos::Jq