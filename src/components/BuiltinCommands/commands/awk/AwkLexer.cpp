#include "commands/awk/AwkLexer.h"

#include <cstdio>
#include <cstdlib>

namespace Haisos::Awk {

const char* TokenKindText(TokenKind kind) {
    switch (kind) {
        case TokenKind::Begin:        return "BEGIN";
        case TokenKind::End:          return "END";
        case TokenKind::Function:     return "function";
        case TokenKind::Getline:     return "getline";
        case TokenKind::If:           return "if";
        case TokenKind::Else:         return "else";
        case TokenKind::While:        return "while";
        case TokenKind::For:          return "for";
        case TokenKind::Do:           return "do";
        case TokenKind::Break:        return "break";
        case TokenKind::Continue:     return "continue";
        case TokenKind::Next:         return "next";
        case TokenKind::Nextfile:     return "nextfile";
        case TokenKind::Exit:         return "exit";
        case TokenKind::Return:       return "return";
        case TokenKind::Delete:      return "delete";
        case TokenKind::In:           return "in";
        case TokenKind::Print:        return "print";
        case TokenKind::Printf:       return "printf";
        case TokenKind::LeftBrace:    return "{";
        case TokenKind::RightBrace:   return "}";
        case TokenKind::LeftParen:    return "(";
        case TokenKind::RightParen:   return ")";
        case TokenKind::LeftBracket: return "[";
        case TokenKind::RightBracket: return "]";
        case TokenKind::Semicolon:    return ";";
        case TokenKind::Comma:        return ",";
        case TokenKind::Plus:         return "+";
        case TokenKind::Minus:        return "-";
        case TokenKind::Star:         return "*";
        case TokenKind::Slash:        return "/";
        case TokenKind::Percent:      return "%";
        case TokenKind::Caret:        return "^";
        case TokenKind::Not:          return "!";
        case TokenKind::Greater:      return ">";
        case TokenKind::Less:         return "<";
        case TokenKind::Pipe:         return "|";
        case TokenKind::Question:     return "?";
        case TokenKind::Colon:        return ":";
        case TokenKind::Tilde:        return "~";
        case TokenKind::NoMatch:      return "!~";
        case TokenKind::Dollar:       return "$";
        case TokenKind::Assign:       return "=";
        case TokenKind::AddAssign:    return "+=";
        case TokenKind::SubAssign:    return "-=";
        case TokenKind::MulAssign:    return "*=";
        case TokenKind::DivAssign:    return "/=";
        case TokenKind::ModAssign:    return "%=";
        case TokenKind::PowAssign:    return "^=";
        case TokenKind::Or:           return "||";
        case TokenKind::And:          return "&&";
        case TokenKind::Equal:        return "==";
        case TokenKind::NotEqual:     return "!=";
        case TokenKind::LessEqual:    return "<=";
        case TokenKind::GreaterEqual: return ">=";
        case TokenKind::Append:       return ">>";
        case TokenKind::Increment:    return "++";
        case TokenKind::Decrement:    return "--";
        default:                      return "";  // the value kinds, Newline, EndOfInput
    }
}

namespace {

// STR's escapes: \\ \" \n \t, and every other byte below 0x20 or 0x7F as
// three octal digits (\000).
std::string DescribeBytes(const std::string& text) {
    std::string out;
    char buffer[8];
    for (const char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20 || c == '\x7f') {
                    std::snprintf(buffer, sizeof(buffer), "\\%03o", static_cast<unsigned char>(c));
                    out += buffer;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

} // namespace

std::string DescribeToken(const Token& token) {
    switch (token.kind) {
        case TokenKind::EndOfInput: return "EOF";
        case TokenKind::Newline:    return "NL";
        case TokenKind::Number:    return "NUM(" + token.text + ")";
        case TokenKind::String:    return "STR(" + DescribeBytes(token.text) + ")";
        case TokenKind::Regex:     return "ERE(" + token.text + ")";
        case TokenKind::Name:      return "NAME(" + token.text + ")";
        case TokenKind::FuncName:  return "FUNC(" + token.text + ")";
        case TokenKind::Builtin:   return "BUILTIN(" + token.text + ")";
        default:                   return TokenKindText(token.kind);
    }
}

std::string DecodeAwkStringEscapes(std::string_view text, std::vector<std::string>& warnings) {
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (c != '\\') {
            out += c;
            ++i;
            continue;
        }
        // A backslash-newline is a continuation; a trailing lone backslash
        // is kept as a backslash.
        if (i + 1 >= text.size()) {
            out += '\\';
            ++i;
            continue;
        }
        const char d = text[i + 1];
        if (d == '\n') {
            i += 2;
            continue;
        }
        if (d == '\r' && i + 2 < text.size() && text[i + 2] == '\n') {
            i += 3;
            continue;
        }
        switch (d) {
            case '"':  out += '"';  i += 2; break;
            case '\\': out += '\\'; i += 2; break;
            case 'a':  out += '\a'; i += 2; break;
            case 'b':  out += '\b'; i += 2; break;
            case 'f':  out += '\f'; i += 2; break;
            case 'n':  out += '\n'; i += 2; break;
            case 'r':  out += '\r'; i += 2; break;
            case 't':  out += '\t'; i += 2; break;
            case 'v':  out += '\v'; i += 2; break;
            case 'x':
                // gawk --posix has no \x escapes: a plain x, no warning.
                out += 'x';
                i += 2;
                break;
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': {
                // One to three octal digits; \0 is a NUL byte.
                int value = 0;
                size_t j = i + 1;
                while (j < text.size() && text[j] >= '0' && text[j] <= '7' &&
                       j - (i + 1) < 3) {
                    value = value * 8 + (text[j] - '0');
                    ++j;
                }
                out += static_cast<char>(value);
                i = j;
                break;
            }
            default:
                out += d;
                warnings.push_back("escape sequence `\\" + std::string(1, d) +
                                   "' treated as plain `" + std::string(1, d) + "'");
                i += 2;
                break;
        }
    }
    return out;
}

namespace {

bool IsNameStart(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

bool IsNameChar(int c) {
    return IsNameStart(c) || (c >= '0' && c <= '9');
}

bool IsDigit(int c) {
    return c >= '0' && c <= '9';
}

// Whether every newline after |kind| is skipped (with blanks, comments and
// continuations between them): after { && || , ; do else.
bool NewlinesSkippedAfter(TokenKind kind) {
    switch (kind) {
        case TokenKind::LeftBrace:
        case TokenKind::And:
        case TokenKind::Or:
        case TokenKind::Comma:
        case TokenKind::Semicolon:
        case TokenKind::Do:
        case TokenKind::Else:
            return true;
        default:
            return false;
    }
}

// A keyword's kind by its exact spelling; TokenKind::Name when the name is
// not one.
TokenKind FindKeyword(const std::string& name) {
    struct Entry {
        const char* word;
        TokenKind kind;
    };
    static const Entry keywords[] = {
        {"BEGIN", TokenKind::Begin},     {"END", TokenKind::End},
        {"function", TokenKind::Function}, {"func", TokenKind::Function},
        {"getline", TokenKind::Getline}, {"if", TokenKind::If},
        {"else", TokenKind::Else},       {"while", TokenKind::While},
        {"for", TokenKind::For},         {"do", TokenKind::Do},
        {"break", TokenKind::Break},     {"continue", TokenKind::Continue},
        {"next", TokenKind::Next},       {"nextfile", TokenKind::Nextfile},
        {"exit", TokenKind::Exit},       {"return", TokenKind::Return},
        {"delete", TokenKind::Delete},   {"in", TokenKind::In},
        {"print", TokenKind::Print},     {"printf", TokenKind::Printf},
    };
    for (const Entry& entry : keywords) {
        if (name == entry.word) {
            return entry.kind;
        }
    }
    return TokenKind::Name;
}

// Whether the name is one of POSIX awk's built-in functions.
bool IsBuiltinFunction(const std::string& name) {
    static const char* const builtins[] = {
        "atan2", "close", "cos", "exp", "fflush", "gsub", "index", "int",
        "length", "log", "match", "rand", "sin", "split", "sprintf", "sqrt",
        "srand", "sub", "substr", "system", "tolower", "toupper",
    };
    for (const char* builtin : builtins) {
        if (name == builtin) {
            return true;
        }
    }
    return false;
}

} // namespace

Lexer::Lexer(AwkSource source) : m_source(std::move(source)) {
    m_lineStarts.push_back(0);
    for (size_t i = 0; i < m_source.text.size(); ++i) {
        if (m_source.text[i] == '\n') {
            m_lineStarts.push_back(i + 1);
        }
    }
    if (m_source.text.empty()) {
        // An empty source gets no newline of its own; EndOfInput sits at
        // line 1, column 0.
        m_emptySource = true;
    }
}

void Lexer::NewlineConsumed() {
    ++m_line;
    m_lineStart = m_pos;
}

Token Lexer::Make(TokenKind kind, const std::string& text, double number, size_t begin, size_t end) {
    Token token;
    token.kind = kind;
    token.text = text;
    token.number = number;
    token.line = m_line;
    token.column = begin - m_lineStart;
    token.begin = begin;
    token.end = end;
    m_last = token;
    return token;
}

void Lexer::Fail(const std::string& message, int line, size_t column) {
    throw AwkSyntaxError(message, m_source.name, line, LineText(line), column);
}

std::vector<AwkWarning> Lexer::TakeWarnings() {
    std::vector<AwkWarning> warnings;
    warnings.swap(m_warnings);
    return warnings;
}

std::string Lexer::LineText(int line) const {
    if (line < 1 || static_cast<size_t>(line) > m_lineStarts.size()) {
        return "";
    }
    const std::string& text = m_source.text;
    const size_t start = m_lineStarts[line - 1];
    size_t end = start;
    while (end < text.size() && text[end] != '\n') {
        ++end;
    }
    return text.substr(start, end - start);
}

void Lexer::SkipSeparators() {
    const std::string& text = m_source.text;
    // A comment consumed here ends at the newline the caller is about to
    // see; nothing else makes one.
    m_commentOpen = false;
    while (!AtEnd()) {
        const char c = text[m_pos];
        if (c == ' ' || c == '\t' || c == '\r') {
            m_lastWasRealNewline = false;
            ++m_pos;
            continue;
        }
        if (c == '#') {
            // A comment runs to the end of the line; its newline stays. A
            // backslash ending a comment belongs to the comment and
            // continues nothing.
            m_lastWasRealNewline = false;
            m_commentBegin = m_pos;
            m_commentOpen = true;
            while (!AtEnd() && text[m_pos] != '\n') {
                ++m_pos;
            }
            continue;
        }
        if (c == '\\') {
            // A backslash-newline (or backslash-\r\n) is removed, anywhere
            // outside a string, a regex or a comment.
            if (m_pos + 1 < text.size() && text[m_pos + 1] == '\n') {
                m_lastWasRealNewline = false;
                m_pos += 2;
                NewlineConsumed();
                continue;
            }
            if (m_pos + 2 < text.size() && text[m_pos + 1] == '\r' && text[m_pos + 2] == '\n') {
                m_lastWasRealNewline = false;
                m_pos += 3;
                NewlineConsumed();
                continue;
            }
            Fail("backslash not last character on line", m_line, Column());
        }
        break;
    }
}

Token Lexer::MakeEndOfInput() {
    Token token;
    token.kind = TokenKind::EndOfInput;
    token.line = m_eofLine;
    token.column = m_eofColumn;
    token.begin = m_source.text.size();
    token.end = m_source.text.size();
    m_last = token;
    return token;
}

Token Lexer::Next() {
    while (true) {
        SkipSeparators();
        if (!AtEnd() && m_source.text[m_pos] == '\n') {
            // A real newline: the source ends in one when nothing follows
            // it, and a comment it ends gives it the comment as its text.
            m_lastWasRealNewline = true;
            m_eofLine = m_line;
            m_eofColumn = m_pos - m_lineStart;
            const std::string comment =
                m_commentOpen ? m_source.text.substr(m_commentBegin, m_pos - m_commentBegin)
                              : std::string();
            if (NewlinesSkippedAfter(m_last.kind)) {
                ++m_pos;
                NewlineConsumed();
                continue;
            }
            Token token = Make(TokenKind::Newline, comment, 0, m_pos, m_pos + 1);
            ++m_pos;
            NewlineConsumed();
            return token;
        }
        if (AtEnd()) {
            // A source that does not end in a real newline (its last byte
            // was a token, blanks, a comment or a continuation) gets one,
            // at its end -- after { && || , ; do else it is skipped like any
            // other. A continuation at the very end keeps this behaviour.
            if (!m_lastWasRealNewline && !m_emptySource && !m_implicitNewlineUsed) {
                m_implicitNewlineUsed = true;
                m_eofLine = m_line;
                m_eofColumn = m_source.text.size() - m_lineStart;
                if (NewlinesSkippedAfter(m_last.kind)) {
                    continue;
                }
                const std::string comment =
                    m_commentOpen
                        ? m_source.text.substr(m_commentBegin,
                                               m_source.text.size() - m_commentBegin)
                        : std::string();
                return Make(TokenKind::Newline, comment, 0, m_source.text.size(),
                            m_source.text.size());
            }
            return MakeEndOfInput();
        }
        break;
    }
    return ScanToken();
}

Token Lexer::ScanToken() {
    m_lastWasRealNewline = false;  // a token, not a newline, was consumed
    const std::string& text = m_source.text;
    const size_t start = m_pos;
    const char c = text[start];
    const auto One = [&](TokenKind kind) {
        m_pos = start + 1;
        return Make(kind, TokenKindText(kind), 0, start, m_pos);
    };
    const auto Two = [&](TokenKind kind) {
        m_pos = start + 2;
        return Make(kind, TokenKindText(kind), 0, start, m_pos);
    };
    const auto Second = [&](char c2) {
        return start + 1 < text.size() && text[start + 1] == c2;
    };
    switch (c) {
        case '{': return One(TokenKind::LeftBrace);
        case '}': return One(TokenKind::RightBrace);
        case '(': return One(TokenKind::LeftParen);
        case ')': return One(TokenKind::RightParen);
        case '[': return One(TokenKind::LeftBracket);
        case ']': return One(TokenKind::RightBracket);
        case ';': return One(TokenKind::Semicolon);
        case ',': return One(TokenKind::Comma);
        case '?': return One(TokenKind::Question);
        case ':': return One(TokenKind::Colon);
        case '~': return One(TokenKind::Tilde);
        case '$': return One(TokenKind::Dollar);
        case '+':
            if (Second('+')) return Two(TokenKind::Increment);
            if (Second('=')) return Two(TokenKind::AddAssign);
            return One(TokenKind::Plus);
        case '-':
            if (Second('-')) return Two(TokenKind::Decrement);
            if (Second('=')) return Two(TokenKind::SubAssign);
            return One(TokenKind::Minus);
        case '*':
            // gawk's ** and **= are not POSIX: two '*' tokens.
            if (Second('=')) return Two(TokenKind::MulAssign);
            return One(TokenKind::Star);
        case '/':
            if (Second('=')) return Two(TokenKind::DivAssign);
            return One(TokenKind::Slash);
        case '%':
            if (Second('=')) return Two(TokenKind::ModAssign);
            return One(TokenKind::Percent);
        case '^':
            if (Second('=')) return Two(TokenKind::PowAssign);
            return One(TokenKind::Caret);
        case '!':
            if (Second('=')) return Two(TokenKind::NotEqual);
            if (Second('~')) return Two(TokenKind::NoMatch);
            return One(TokenKind::Not);
        case '>':
            if (Second('>')) return Two(TokenKind::Append);
            if (Second('=')) return Two(TokenKind::GreaterEqual);
            return One(TokenKind::Greater);
        case '<':
            if (Second('=')) return Two(TokenKind::LessEqual);
            return One(TokenKind::Less);
        case '|':
            if (Second('|')) return Two(TokenKind::Or);
            return One(TokenKind::Pipe);
        case '=':
            if (Second('=')) return Two(TokenKind::Equal);
            return One(TokenKind::Assign);
        case '&':
            if (Second('&')) return Two(TokenKind::And);
            // A lone '&' starts no token: gawk's syntax error, caret on it.
            Fail("syntax error", m_line, Column());
        default:
            break;
    }
    if (IsDigit(c) || (c == '.' && start + 1 < text.size() && IsDigit(text[start + 1]))) {
        return ScanNumber();
    }
    if (IsNameStart(c)) {
        return ScanName();
    }
    if (c == '"') {
        return ScanString();
    }
    Fail("invalid char '" + std::string(1, c) + "' in expression", m_line, Column());
}

Token Lexer::ScanNumber() {
    const std::string& text = m_source.text;
    const size_t start = m_pos;
    size_t end = m_pos;
    while (end < text.size() && IsDigit(text[end])) {
        ++end;
    }
    if (end < text.size() && text[end] == '.') {
        ++end;
        while (end < text.size() && IsDigit(text[end])) {
            ++end;
        }
    }
    if (end < text.size() && (text[end] == 'e' || text[end] == 'E')) {
        // An exponent only when digits follow it ("1e" is 1 then the name e).
        size_t scan = end + 1;
        if (scan < text.size() && (text[scan] == '+' || text[scan] == '-')) {
            ++scan;
        }
        size_t digits = scan;
        while (digits < text.size() && IsDigit(text[digits])) {
            ++digits;
        }
        if (digits > scan) {
            end = digits;
        }
    }
    const std::string spelling = text.substr(start, end - start);
    m_pos = end;
    return Make(TokenKind::Number, spelling, std::strtod(spelling.c_str(), nullptr), start, end);
}

Token Lexer::ScanName() {
    const std::string& text = m_source.text;
    const size_t start = m_pos;
    size_t end = start + 1;
    while (end < text.size() && IsNameChar(text[end])) {
        ++end;
    }
    const std::string name = text.substr(start, end - start);
    m_pos = end;
    TokenKind kind = TokenKind::Name;
    const TokenKind keyword = FindKeyword(name);
    if (keyword != TokenKind::Name) {
        kind = keyword;
    } else if (IsBuiltinFunction(name)) {
        kind = TokenKind::Builtin;  // whatever follows
    } else if (!AtEnd() && text[m_pos] == '(') {
        // A call of a user function touches its '('.
        kind = TokenKind::FuncName;
    }
    return Make(kind, name, 0, start, end);
}

Token Lexer::ScanString() {
    const std::string& text = m_source.text;
    const size_t start = m_pos;  // the opening quote
    const int line = m_line;
    const size_t column = Column();
    ++m_pos;
    std::string body;
    while (true) {
        if (AtEnd() || text[m_pos] == '\n') {
            Fail("unterminated string", line, column);
        }
        const char c = text[m_pos];
        if (c == '"') {
            ++m_pos;
            break;
        }
        if (c == '\\') {
            // A backslash-newline inside a string is a continuation; the
            // rest of the escape decoding is DecodeAwkStringEscapes' work.
            if (m_pos + 1 < text.size() && text[m_pos + 1] == '\n') {
                m_pos += 2;
                NewlineConsumed();
                continue;
            }
            if (m_pos + 2 < text.size() && text[m_pos + 1] == '\r' && text[m_pos + 2] == '\n') {
                m_pos += 3;
                NewlineConsumed();
                continue;
            }
            if (m_pos + 1 >= text.size()) {
                Fail("unterminated string", line, column);
            }
            body += c;
            body += text[m_pos + 1];
            m_pos += 2;
            continue;
        }
        body += c;
        ++m_pos;
    }
    std::vector<std::string> escapeWarnings;
    const std::string value = DecodeAwkStringEscapes(body, escapeWarnings);
    for (const std::string& message : escapeWarnings) {
        m_warnings.push_back(AwkWarning{m_source.name, line, message});
    }
    Token token;
    token.kind = TokenKind::String;
    token.text = value;
    token.line = line;
    token.column = column;
    token.begin = start;
    token.end = m_pos;
    m_last = token;
    return token;
}

Token Lexer::ScanRegex(const Token& slash) {
    if (slash.kind != TokenKind::Slash && slash.kind != TokenKind::DivAssign) {
        throw std::logic_error("ScanRegex: not a '/' or '/=' token");
    }
    if (m_last.kind != slash.kind || m_last.begin != slash.begin || m_last.end != slash.end) {
        throw std::logic_error("ScanRegex: not the token Next() just returned");
    }
    const std::string& text = m_source.text;
    const int line = slash.line;
    const size_t column = slash.column;
    m_pos = slash.begin + 1;  // right after the '/'; for '/=' the '=' is first
    std::string body;
    bool inBracket = false;
    while (true) {
        if (AtEnd() || text[m_pos] == '\n') {
            Fail("unterminated regexp", line, column + 1);
        }
        const char c = text[m_pos];
        if (c == '\\') {
            // A backslash and the byte after it are one pair: \/ is stored
            // as /, every other pair byte for byte; a backslash-newline is
            // removed.
            if (m_pos + 1 < text.size() && text[m_pos + 1] == '\n') {
                m_pos += 2;
                NewlineConsumed();
                continue;
            }
            if (m_pos + 2 < text.size() && text[m_pos + 1] == '\r' && text[m_pos + 2] == '\n') {
                m_pos += 3;
                NewlineConsumed();
                continue;
            }
            if (m_pos + 1 >= text.size()) {
                Fail("unterminated regexp", line, column + 1);
            }
            const char d = text[m_pos + 1];
            m_pos += 2;
            if (d == '/') {
                body += '/';
            } else {
                body += c;
                body += d;
            }
            continue;
        }
        if (inBracket) {
            if (c == ']') {
                inBracket = false;
                body += c;
                ++m_pos;
                continue;
            }
            if (c == '[' && m_pos + 1 < text.size() &&
                (text[m_pos + 1] == ':' || text[m_pos + 1] == '.' || text[m_pos + 1] == '=')) {
                // A [: ... :], [. ... .] or [= ... =] item, skipped whole; a
                // malformed one leaves the '[' a plain member.
                const char closer = text[m_pos + 1];
                size_t scan = m_pos + 2;
                bool closed = false;
                while (scan + 1 < text.size() && text[scan] != ']' && text[scan] != '\n') {
                    if (text[scan] == closer && text[scan + 1] == ']') {
                        closed = true;
                        break;
                    }
                    ++scan;
                }
                if (closed) {
                    body.append(text, m_pos, scan + 2 - m_pos);
                    m_pos = scan + 2;
                } else {
                    body += c;
                    ++m_pos;
                }
                continue;
            }
            body += c;
            ++m_pos;
            continue;
        }
        if (c == '/') {
            ++m_pos;
            break;
        }
        if (c == '[') {
            inBracket = true;
            body += c;
            ++m_pos;
            // A ']' right after '[' or '[^' is a member.
            if (!AtEnd() && text[m_pos] == '^') {
                body += '^';
                ++m_pos;
            }
            if (!AtEnd() && text[m_pos] == ']') {
                body += ']';
                ++m_pos;
            }
            continue;
        }
        body += c;
        ++m_pos;
    }
    Token token;
    token.kind = TokenKind::Regex;
    token.text = body;
    token.line = line;
    token.column = column;
    token.begin = slash.begin;
    token.end = m_pos;
    m_last = token;
    return token;
}

} // namespace Haisos::Awk