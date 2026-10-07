#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "commands/hsh/HshWord.h"

namespace Haisos::Hsh {

enum class TokenKind {
    Word, IoNumber, Newline, EndOfInput,
    Semicolon,       // ;
    DoubleSemicolon, // ;;
    Ampersand,       // &
    AndIf,           // &&
    Pipe,            // |
    OrIf,            // ||
    LeftParen,       // (
    RightParen,      // )
    Less,            // <
    Great,           // >
    DoubleGreat,     // >>
    Clobber,         // >|
    LessGreat,       // <>
    LessAnd,         // <&
    GreatAnd,        // >&
    DoubleLess,      // <<
    DoubleLessDash,  // <<-
    TripleLess,      // <<<  (bash's here-string)
    AndGreat,        // &>   (bash's: stdout and stderr to a file)
};

// The operator's text (";", "&&", "<<-" ...); "" for Word, IoNumber, Newline, EndOfInput.
const char* OperatorText(TokenKind kind);

// Whether it is one of the redirection operators (Less ... AndGreat).
bool IsRedirectionOperator(TokenKind kind);

enum class ReservedWord { If, Then, Else, Elif, Fi, Do, Done, Case, Esac, While, Until, For, In, LeftBrace, RightBrace, Bang };
const char* ReservedWordText(ReservedWord word);  // "if", ..., "{", "}", "!"

struct Token {
    TokenKind kind = TokenKind::EndOfInput;
    int line = 0;                           // the line the token starts on
    Word word;                              // Word
    int ioNumber = -1;                      // IoNumber: 0-9
    std::shared_ptr<HereDocument> hereDoc;  // the Word right after << or <<-: its heredoc, body filled at the next newline
    // Byte offsets in the Lexer's source (the string given to its constructor):
    // the token's first character and one past its last, as written (quotes,
    // escapes, line continuations inside it included; for a Newline the '\n'
    // itself; for EndOfInput both are the source's size). The parser cuts
    // ListItem::sourceText and FunctionDefinition::sourceText out of the
    // source with them.
    size_t begin = 0;
    size_t end = 0;
};

// The reserved word the token spells when it is a Word of exactly that
// unquoted text ("if", "{", "!" ...). Whether it IS a reserved word depends on
// where it stands, which only the parser knows (`echo if` is a plain word).
std::optional<ReservedWord> AsReservedWord(const Token& token);

// For tests and debugging; the format: a Word is W(<DescribeWord>), an IoNumber
// IO(<n>), a Newline NL, the end EOF, an operator its OperatorText.
std::string DescribeToken(const Token& token);

struct LexerOptions {
    int firstLine = 1;        // the line number of the first line of the source
    // An interactive shell's input: two endings a script accepts become
    // incomplete-input errors, so the shell reads more -- a heredoc whose
    // delimiter line has not come yet, and a source ending in a backslash-newline.
    bool interactive = false;
};

class Lexer {
public:
    explicit Lexer(std::string source, LexerOptions options = {});
    Lexer(Lexer&& other) noexcept;
    Lexer& operator=(Lexer&& other) noexcept;
    ~Lexer();

    // The next token; EndOfInput forever once the source is used up. Throws
    // ShellError on a lexical error.
    Token Next();

    // The line the lexer is at: where the next token would start.
    int Line() const;

private:
    struct Impl;
    friend struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Haisos::Hsh
