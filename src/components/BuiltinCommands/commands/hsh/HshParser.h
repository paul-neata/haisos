#pragma once

#include <string>

#include "commands/hsh/HshAst.h"
#include "commands/hsh/HshLexer.h"

namespace Haisos::Hsh {

// ## Source text
//
// `ListItem::sourceText` and `FunctionDefinition::sourceText` hold the text
// of the command exactly as written: they are cut from the source string the
// parser was given, never rebuilt from the tree. The lexer stamps every token
// with its byte offsets (`Token::begin`/`Token::end`); the parser takes a
// command's text from the `begin` of its first token through the `end` of its
// last (so comments, blanks and the `;`/`&` that follows stay out), plus,
// when heredoc bodies end after that range, one newline and each such body
// region in order (`HereDocument::sourceBegin`/`sourceEnd`). Parsing a
// sourceText again gives the same command; a background item needing the
// shell at run time is run as `hsh -c <sourceText>`.

struct ParserOptions {
    int firstLine = 1;
    bool interactive = false;   // passed to the lexer (see LexerOptions)
    // How the end of the input is named in messages: "end of file" for a
    // script or -c, "\")\"" when checking the text of a $(...) (see HshParser.cpp).
    std::string endOfInputName = "end of file";
};

struct ParseResult {
    enum class Status { Command, EndOfInput, Error };
    Status status = Status::EndOfInput;
    CommandList commands;       // Command: the next complete command
    std::string errorMessage;   // Error: what follows "hsh: <line>: ", e.g. "Syntax error: \"fi\" unexpected"
    int errorLine = 0;          // Error
    bool incomplete = false;    // Error: the input ended where more was needed
};

class Parser {
public:
    explicit Parser(std::string source, ParserOptions options = {});
    Parser(Parser&&) noexcept;
    Parser& operator=(Parser&&) noexcept;
    ~Parser();

    // Parses the next complete command: an and-or list, or several joined by
    // ; or &, up to the newline that ends it (heredoc bodies included) or the
    // end of input -- what dash reads before running anything. Empty lines and
    // comment-only lines are skipped. EndOfInput once the source is used up;
    // after an Error, every later call returns the same Error.
    ParseResult ParseNext();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// Every complete command of |source| in one list (Status Command, an empty
// list for an empty source), or the first error. For `eval`, `.`, the text of
// a command substitution, and the tests.
ParseResult ParseProgram(const std::string& source, ParserOptions options = {});

} // namespace Haisos::Hsh
