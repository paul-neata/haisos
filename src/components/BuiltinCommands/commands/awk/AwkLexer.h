#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "commands/awk/AwkError.h"

namespace Haisos::Awk {

enum class TokenKind {
    EndOfInput, Newline,
    Number, String, Regex, Name, FuncName, Builtin,
    // keywords
    Begin, End, Function, Getline, If, Else, While, For, Do, Break, Continue,
    Next, Nextfile, Exit, Return, Delete, In, Print, Printf,
    // punctuation
    LeftBrace, RightBrace, LeftParen, RightParen, LeftBracket, RightBracket,
    Semicolon, Comma, Plus, Minus, Star, Slash, Percent, Caret, Not,
    Greater, Less, Pipe, Question, Colon, Tilde, NoMatch, Dollar,
    Assign, AddAssign, SubAssign, MulAssign, DivAssign, ModAssign, PowAssign,
    Or, And, Equal, NotEqual, LessEqual, GreaterEqual, Append, Increment, Decrement,
};

// The spelling of a keyword or punctuation kind ("BEGIN", "getline", "{",
// "!~", ">>", "/="); "" for EndOfInput, Newline and the six value kinds.
const char* TokenKindText(TokenKind kind);

struct Token {
    TokenKind kind = TokenKind::EndOfInput;
    // Number: the text as written ("1e3"); String: the value, escapes
    // decoded; Regex: the regex text (see Lexer::ScanRegex); Name/FuncName/
    // Builtin: the name; keywords: the keyword as written ("func" or
    // "function"); a Newline ending a comment: the comment, from the '#'
    // to before the newline (every other Newline's text is empty).
    std::string text;
    double number = 0;   // Number: its value
    int line = 1;        // the line the token starts on, from 1
    size_t column = 0;   // byte offset of its first byte within that line
    size_t begin = 0;    // byte offsets in the source: first byte, one past
    size_t end = 0;      // the last
};

// For tests: NUM(<text>), STR(<escaped value>), ERE(<text>), NAME(x),
// FUNC(f), BUILTIN(length), NL, EOF, and TokenKindText for the rest (a
// Function token is "function" whichever way it was spelled). STR escapes \\
// \" \n \t and every other byte below 0x20 or 0x7F as \ooo (3 octal digits).
std::string DescribeToken(const Token& token);

// awk's string escapes, as gawk --posix decodes a string literal's body (and
// -v values, -F's value, var=value operands): \" \\ \a \b \f \n \r \t \v,
// \ooo (1 to 3 octal digits, \0 is a NUL byte), a backslash-newline removed
// (a continuation); any other \c is c, with the warning
// "escape sequence `\c' treated as plain `c'" appended to |warnings| --
// except \x, which is a plain x with no warning (gawk --posix has no \x
// escapes). A trailing lone backslash is kept as a backslash.
std::string DecodeAwkStringEscapes(std::string_view text, std::vector<std::string>& warnings);

// The lexer of POSIX awk, as gawk --posix reads a program: tokens, string
// escapes, comments, backslash-newline continuations, newlines as tokens
// except after { && || , ; do else, and the regex-versus-division ambiguity
// left to the parser: a '/' or '/=' the parser hands back to ScanRegex
// becomes a Regex token. A plain class (it implements no interface), held by
// value.
class Lexer {
public:
    explicit Lexer(AwkSource source);

    // The next token. Throws AwkSyntaxError on a lexical error.
    Token Next();

    // Rescans as a regex the '/' (Slash) or '/=' (DivAssign) token that Next()
    // has just returned -- the parser calls it when such a token stands where
    // an operand is expected. Reads from right after the '/' (so for '/=' the
    // '=' is the regex's first byte) to the closing '/', and returns a Regex
    // token (line, column and begin of the '/'). Throws std::logic_error when
    // the last token returned was neither.
    Token ScanRegex(const Token& slash);

    // The warnings found so far (string escapes), in order; cleared.
    std::vector<AwkWarning> TakeWarnings();

    const AwkSource& Source() const { return m_source; }
    // The text of line |line| (from 1) without its newline; "" past the end.
    std::string LineText(int line) const;

private:
    bool AtEnd() const { return m_pos >= m_source.text.size(); }
    size_t Column() const { return m_pos - m_lineStart; }
    void NewlineConsumed();  // a '\n' was consumed: on to the next line
    // The EndOfInput token, at m_eofLine/m_eofColumn.
    Token MakeEndOfInput();
    // Blanks, comments and backslash-newline continuations between tokens;
    // a backslash before anything but a newline is a lexical error.
    void SkipSeparators();
    Token ScanToken();
    Token ScanNumber();
    Token ScanName();
    Token ScanString();
    Token Make(TokenKind kind, const std::string& text, double number, size_t begin, size_t end);
    [[noreturn]] void Fail(const std::string& message, int line, size_t column);

    AwkSource m_source;
    std::vector<size_t> m_lineStarts;  // of LineText: byte offset of each line
    size_t m_pos = 0;
    int m_line = 1;
    size_t m_lineStart = 0;
    std::vector<AwkWarning> m_warnings;
    Token m_last;                 // the last token Next() returned
    bool m_implicitNewlineUsed = false;  // the source's added newline, given?
    bool m_emptySource = false;          // an empty source gets no newline of its own
    // Whether the source ends in a real newline, decided as the end is
    // reached: the last byte consumed was a real '\n' (a Newline token or a
    // skipped newline), not a token, blanks, a comment or a continuation.
    // A backslash ending a comment belongs to the comment and continues
    // nothing, so such a source ends in a real newline.
    bool m_lastWasRealNewline = false;
    size_t m_commentBegin = 0;    // the '#' of a comment just consumed
    bool m_commentOpen = false;
    int m_eofLine = 1;            // where EndOfInput sits: the last line, its end
    size_t m_eofColumn = 0;
};

} // namespace Haisos::Awk