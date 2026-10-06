#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Haisos::Hsh {

enum class WordPartKind {
    Literal,             // unquoted text, as written
    Quoted,              // text that was quoted: '...', \c outside quotes, the plain text inside "..." or a heredoc body
    DoubleQuoted,        // "...": its contents are |parts|
    Parameter,           // $name, ${...}
    CommandSubstitution, // $(...) or `...`
    Arithmetic,          // $((...)): the expression is |parts|
};

enum class ParameterOp {
    None,                 // $name, ${name}
    Length,               // ${#name}
    UseDefault,           // ${name:-word}
    UseDefaultIfUnset,    // ${name-word}
    AssignDefault,        // ${name:=word}
    AssignDefaultIfUnset, // ${name=word}
    ErrorIfNull,          // ${name:?word}
    ErrorIfUnset,         // ${name?word}
    UseAlternative,       // ${name:+word}
    UseAlternativeIfSet,  // ${name+word}
    RemoveSmallestSuffix, // ${name%word}
    RemoveLargestSuffix,  // ${name%%word}
    RemoveSmallestPrefix, // ${name#word}
    RemoveLargestPrefix,  // ${name##word}
    Bad,                  // a ${...} that makes no sense: expanding it fails with "Bad substitution"
};

struct WordPart {
    WordPartKind kind = WordPartKind::Literal;
    // Literal, Quoted: the characters. Parameter: the name -- an identifier,
    // digits (a positional parameter, "10" for ${10}), or one of @ * # ? - $ ! 0
    // (empty for Bad). CommandSubstitution: the source of the command inside,
    // exactly as written between "$(" and ")" (heredoc bodies included); for
    // backquotes the text after backquote processing (see HshLexer).
    std::string text;
    // DoubleQuoted: the contents. Parameter: the operand word (the "word" of
    // ${name:-word}), empty when none. Arithmetic: the expression.
    std::vector<WordPart> parts;
    ParameterOp op = ParameterOp::None;  // Parameter only
    bool backquoted = false;             // CommandSubstitution: `...` rather than $(...)
    int line = 0;                        // CommandSubstitution: the line its text starts on
};

struct Word {
    std::vector<WordPart> parts;
    std::string source;  // the word exactly as written, quotes included (for messages and debugging)
    int line = 0;        // the line it starts on
};

// What a heredoc redirection (<<, <<-) reads. Created by the lexer when it
// reads the delimiter word, filled at the next newline.
struct HereDocument {
    std::string delimiter;   // the delimiter word with its quotes removed
    bool quoted = false;     // some of the delimiter was quoted: the body is taken as it is, unexpanded
    bool stripTabs = false;  // <<-: leading tabs were removed from every body line and from the delimiter line
    std::string rawBody;     // the body lines, each with its '\n', the delimiter line excluded
    Word body;               // !quoted: rawBody lexed for $-expansions and `...` (Quoted text + expansion parts); quoted: one Quoted part holding rawBody (no part when rawBody is empty)
    bool complete = false;   // the body has been read (or the input ended first)
    bool terminated = false; // the delimiter line was found
    // Byte offsets in the lexer's source of the body region: from the first
    // body line through the delimiter line and its '\n' (end exclusive; up to
    // the end of input when not terminated). The parser copies this region to
    // rebuild a command's text (ListItem::sourceText).
    size_t sourceBegin = 0;
    size_t sourceEnd = 0;
};

// A name a shell variable or function may have: [A-Za-z_][A-Za-z0-9_]*.
bool IsValidShellName(std::string_view name);

// The word's text when it is made of unquoted literal text only (no quotes,
// no expansions), else nullopt -- what reserved words, for-loop variables,
// function names and assignment prefixes are recognized by.
std::optional<std::string> LiteralText(const Word& word);

// For tests and debugging; the format is in "Word and token descriptions" in
// HshLexer.h: Literal as L'<text>', Quoted as Q'<text>', DoubleQuoted as D[...],
// Parameter as P(...), CommandSubstitution as C'<text>' or B'<text>' (backquoted),
// Arithmetic as A[...].
std::string DescribeWord(const Word& word);

} // namespace Haisos::Hsh
