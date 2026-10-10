#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Haisos::Jq {

// The tokens of the jq language, as its lexer sees it: one kind per shape
// the parser needs to tell apart. The names are Haisos's own; the names jq
// prints in its error messages (INVALID_CHARACTER, IDENT, FIELD, ...) are
// reproduced only inside TokenNameInMessage, as strings.
enum class TokenType {
    End,                 // end of input
    Invalid,             // a byte that starts no token, an unmatched closer, a bad escape
    Identifier,          // foo, a::b
    Field,               // .foo (text is "foo")
    Variable,            // $foo (text is "foo")
    LocVariable,         // $__loc__
    Number,               // a number, text as written
    Format,              // @base64 (text is "base64")
    StringStart,         // the opening "
    StringText,          // text between quotes/interpolations, escapes already decoded
    InterpolationStart,  // \(
    InterpolationEnd,    // the ) closing \(
    StringEnd,           // the closing "
    Keyword,             // as def if then elif else end and or reduce foreach try
                         // catch label import include module __loc__
    Operator,            // != == // //= |= += -= *= /= %= <= >= .. ?//
    Char,                // one of . ? = ; , : | + - * / % $ < > ( ) [ ] { }
};

struct Token {
    TokenType type = TokenType::End;
    std::string text;   // see TokenType; Keyword/Operator/Char: the spelling
    size_t begin = 0;   // byte offsets into the program text
    size_t end = 0;
};

// How jq's messages name a token after "unexpected " / "expecting "
// (observed output): End "end of file"; Invalid "INVALID_CHARACTER";
// Identifier "IDENT"; Field "FIELD"; Variable "BINDING"; LocVariable
// "$__loc__"; Number "LITERAL"; Format "FORMAT"; StringStart
// "QQSTRING_START"; StringText "QQSTRING_TEXT"; InterpolationStart
// "QQSTRING_INTERP_START"; InterpolationEnd "QQSTRING_INTERP_END";
// StringEnd "QQSTRING_END"; Keyword and Operator: the spelling unquoted
// (if, then, ==, ..); Char: the character in single quotes ('}', '|').
std::string TokenNameInMessage(const Token& token);

// The lexer of jq programs. Next() returns the tokens in order; it keeps a
// stack of the open brackets so that a closer that does not close the
// innermost open bracket is Invalid, and lexes string literals
// (interpolations included) as the StringStart/StringText/
// InterpolationStart/InterpolationEnd/StringEnd sequence, returning to the
// string after each interpolation closes. A bad escape in a string is
// returned as an Invalid token whose begin is the escape run's first '\',
// with Error() holding jq's message for it; every other Invalid has an empty
// Error(). A plain class (no interface), owned by value.
class Lexer {
public:
    explicit Lexer(std::string_view program);

    Token Next();

    // The bad-escape message of the Invalid token just returned; empty for
    // any other Invalid token.
    const std::string& Error() const { return m_error; }

private:
    enum class Bracket { Paren, Square, Brace, Interp };

    Token LexIdent();      // an identifier, keyword or $name
    Token LexNumber();     // digits, a fraction, an exponent -- text as written
    Token LexField();      // '.' followed by a name
    Token LexFormat();     // '@' followed by a name
    Token LexStringStart();
    Token LexInString();   // StringText, InterpolationStart, StringEnd, End,
                           // or the Invalid of a bad escape
    Token MakeOperator(size_t begin, size_t length);
    Token MakeChar(size_t begin, size_t length);
    Token MakeInvalid(size_t begin, size_t length);
    // Scans the escape run starting at m_pos (its first '\'), appending the
    // decoded bytes to |text|. Returns false when the run holds a bad
    // escape: then m_error holds jq's message and m_pos is left at the
    // run's end. '\(' is not an escape: the run ends before it.
    bool ScanEscapeRun(std::string& text);
    void PushBracket(Bracket kind) { m_brackets.push_back(kind); }
    // The closer of |closing|, matched against the innermost open bracket:
    // a match pops and returns false, a mismatch returns true (Invalid).
    bool CloseBracket(char closing, Bracket& popped);
    bool AtEnd() const { return m_pos >= m_program.size(); }
    char Peek(size_t ahead = 0) const;

    std::string_view m_program;
    size_t m_pos = 0;
    bool m_inString = false;
    std::vector<Bracket> m_brackets;
    std::string m_error;      // the bad-escape message, cleared by every Next()
    size_t m_errorOffset = 0; // the escape run's first '\'
};

} // namespace Haisos::Jq