#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <vector>
#include "commands/awk/AwkError.h"
#include "commands/awk/AwkLexer.h"

namespace {

using Haisos::Awk::AwkSource;
using Haisos::Awk::AwkSyntaxError;
using Haisos::Awk::Lexer;
using Haisos::Awk::Token;
using Haisos::Awk::TokenKind;

AwkSource Source(const std::string& text) {
    return AwkSource{Haisos::Awk::kCommandLineSourceName, text};
}

// Every token of |text| through the first EndOfInput; a Slash or DivAssign
// is rescanned as a regex when |rescanRegex|, as the parser will; the
// lexer's warnings, when asked for, land in |warnings|.
std::vector<Token> AllTokens(const std::string& text, bool rescanRegex,
                             std::vector<Haisos::Awk::AwkWarning>* warnings) {
    Lexer lexer(Source(text));
    std::vector<Token> tokens;
    while (true) {
        Token token = lexer.Next();
        if (rescanRegex && (token.kind == TokenKind::Slash || token.kind == TokenKind::DivAssign)) {
            token = lexer.ScanRegex(token);
        }
        const bool end = token.kind == TokenKind::EndOfInput;
        tokens.push_back(token);
        if (end) {
            if (warnings) {
                *warnings = lexer.TakeWarnings();
            }
            return tokens;
        }
    }
}

std::vector<Token> AllTokens(const std::string& text) {
    return AllTokens(text, false, nullptr);
}

std::string JoinTokens(const std::vector<Token>& tokens) {
    std::string out;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) {
            out += " ";
        }
        out += Haisos::Awk::DescribeToken(tokens[i]);
    }
    return out;
}

std::string Lex(const std::string& text) {
    return JoinTokens(AllTokens(text));
}

std::string LexRegex(const std::string& text) {
    return JoinTokens(AllTokens(text, true, nullptr));
}

// Lexes |source| and expects the AwkSyntaxError with the given text, line
// and column.
void ExpectError(const std::string& source, const std::string& message, int line, size_t column,
                 bool rescanRegex = false) {
    try {
        AllTokens(source, rescanRegex, nullptr);
    } catch (const AwkSyntaxError& error) {
        EXPECT_EQ(std::string(error.what()), message);
        EXPECT_EQ(error.Line(), line);
        EXPECT_EQ(error.Column(), column);
        return;
    }
    FAIL() << "no AwkSyntaxError from " << source;
}

} // namespace

TEST(AwkLexerTest, Punctuation) {
    EXPECT_EQ(Lex("{ } ( ) [ ] ; , + - * / % ^ ! > < | ? : ~ $ = += -= *= /= %= ^= || && == != <= >= >> ++ -- !~"),
        "{ } ( ) [ ] ; , + - * / % ^ ! > < | ? : ~ $ = += -= *= /= %= ^= || && == != <= >= >> ++ -- !~ NL EOF");
    // gawk's ** and **= are not POSIX: two '*' tokens (a syntax error later).
    EXPECT_EQ(Lex("a**b"), "NAME(a) * * NAME(b) NL EOF");
    EXPECT_EQ(Lex("a!==b"), "NAME(a) != = NAME(b) NL EOF");
}

TEST(AwkLexerTest, KeywordsNamesBuiltins) {
    EXPECT_EQ(Lex("BEGIN END function func getline if else while for do break continue next nextfile"
                  " exit return delete in print printf"),
        "BEGIN END function function getline if else while for do break continue next nextfile"
        " exit return delete in print printf NL EOF");
    EXPECT_EQ(Lex("length substr(x) foo foo(1) foo (1) _a1 gensub(x)"),
        "BUILTIN(length) BUILTIN(substr) ( NAME(x) ) NAME(foo) FUNC(foo) ( NUM(1) ) NAME(foo)"
        " ( NUM(1) ) NAME(_a1) FUNC(gensub) ( NAME(x) ) NL EOF");
}

TEST(AwkLexerTest, Numbers) {
    EXPECT_EQ(Lex("1 1.5 .5 1. 1e3 1E-2 1e 1.2.3 0x11 007"),
        "NUM(1) NUM(1.5) NUM(.5) NUM(1.) NUM(1e3) NUM(1E-2) NUM(1) NAME(e) NUM(1.2) NUM(.3)"
        " NUM(0) NAME(x11) NUM(007) NL EOF");
    const std::vector<Token> tokens = AllTokens("1e3 .5 007 1E-2");
    ASSERT_EQ(tokens.size(), 6u);
    EXPECT_EQ(tokens[0].number, 1000);
    EXPECT_EQ(tokens[1].number, 0.5);
    EXPECT_EQ(tokens[2].number, 7);
    EXPECT_EQ(tokens[3].number, 0.01);
}

TEST(AwkLexerTest, Strings) {
    // No \x escapes in POSIX mode: a plain x with no warning.
    std::vector<Haisos::Awk::AwkWarning> cleanWarnings;
    const std::vector<Token> clean =
        AllTokens(R"("a\"b\\c\n\t\101\0612\x41")", false, &cleanWarnings);
    EXPECT_EQ(JoinTokens(clean), "STR(a\\\"b\\\\c\\n\\tA12x41) NL EOF");
    EXPECT_TRUE(cleanWarnings.empty());

    // An escape POSIX does not have is its plain byte, with a warning.
    std::vector<Haisos::Awk::AwkWarning> warnings;
    const std::vector<Token> tokens = AllTokens("\"a\\/b\" \"\\q\"", false, &warnings);
    EXPECT_EQ(JoinTokens(tokens), "STR(a/b) STR(q) NL EOF");
    ASSERT_EQ(warnings.size(), 2u);
    EXPECT_EQ(warnings[0].sourceName, Haisos::Awk::kCommandLineSourceName);
    EXPECT_EQ(warnings[0].line, 1);
    EXPECT_EQ(warnings[0].message, "escape sequence `\\/' treated as plain `/'");
    EXPECT_EQ(warnings[1].line, 1);
    EXPECT_EQ(warnings[1].message, "escape sequence `\\q' treated as plain `q'");

    // \0 is a NUL byte.
    const std::vector<Token> nul = AllTokens("\"\\0\"");
    ASSERT_EQ(nul.size(), 3u);
    EXPECT_EQ(nul[0].text, std::string(1, '\0'));
    EXPECT_EQ(JoinTokens(nul), "STR(\\000) NL EOF");

    // A backslash-newline inside a string is a continuation: the next token
    // is on line 2 (a documented exception).
    const std::vector<Token> continued = AllTokens("\"a\\\nb\" x");
    EXPECT_EQ(JoinTokens(continued), "STR(ab) NAME(x) NL EOF");
    EXPECT_EQ(continued[0].line, 1);
    EXPECT_EQ(continued[1].line, 2);
}

TEST(AwkLexerTest, StringEscapeDecoder) {
    std::vector<std::string> warnings;
    EXPECT_EQ(Haisos::Awk::DecodeAwkStringEscapes("a\\tb", warnings), "a\tb");
    EXPECT_TRUE(warnings.empty());
    EXPECT_EQ(Haisos::Awk::DecodeAwkStringEscapes("\\101", warnings), "A");
    EXPECT_EQ(Haisos::Awk::DecodeAwkStringEscapes("\\1234", warnings), "S4");
    EXPECT_EQ(Haisos::Awk::DecodeAwkStringEscapes("x\\", warnings), "x\\");
    EXPECT_EQ(Haisos::Awk::DecodeAwkStringEscapes("\\e", warnings), "e");
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_EQ(warnings[0], "escape sequence `\\e' treated as plain `e'");
}
TEST(AwkLexerTest, NewlinesAndComments) {
    const std::vector<Token> tokens = AllTokens("a # c\nb\n\n");
    EXPECT_EQ(JoinTokens(tokens), "NAME(a) NL NAME(b) NL NL EOF");
    EXPECT_EQ(tokens[0].line, 1);
    EXPECT_EQ(tokens[1].line, 1);
    EXPECT_EQ(tokens[2].line, 2);
    EXPECT_EQ(tokens[3].line, 2);
    EXPECT_EQ(tokens[4].line, 3);
    EXPECT_EQ(tokens[5].line, 3);
    EXPECT_EQ(tokens[5].column, 0u);

    // After { && || , ; do else every newline is skipped.
    EXPECT_EQ(Lex("{\n\n x &&\n y ||\n z ,\n w ;\n v do\n u else\n t\n}"),
        "{ NAME(x) && NAME(y) || NAME(z) , NAME(w) ; NAME(v) do NAME(u) else NAME(t)"
        " NL } NL EOF");
    EXPECT_EQ(Lex("a ;# c\n b"), "NAME(a) ; NAME(b) NL EOF");

    // A backslash ending a comment belongs to the comment: it continues
    // nothing, and the newline it precedes ends the source.
    const std::vector<Token> commentBackslash = AllTokens("a # c \\\n");
    EXPECT_EQ(JoinTokens(commentBackslash), "NAME(a) NL EOF");
    ASSERT_EQ(commentBackslash.size(), 3u);
    EXPECT_EQ(commentBackslash[1].line, 1);
    EXPECT_EQ(commentBackslash[1].column, 7u);
    EXPECT_EQ(commentBackslash[1].text, "# c \\");
    EXPECT_EQ(commentBackslash[2].line, 1);
    EXPECT_EQ(commentBackslash[2].column, 7u);
    EXPECT_EQ(Lex("a # c \\\nb"), "NAME(a) NL NAME(b) NL EOF");

    // A Newline that ends a comment carries the comment as its text; every
    // other Newline's text is empty.
    const std::vector<Token> commentToEnd = AllTokens("a # c");
    ASSERT_EQ(commentToEnd.size(), 3u);
    EXPECT_EQ(commentToEnd[1].text, "# c");
    const std::vector<Token> plainNewline = AllTokens("a\n");
    ASSERT_EQ(plainNewline.size(), 3u);
    EXPECT_EQ(plainNewline[1].text, "");
}

TEST(AwkLexerTest, Continuations) {
    const std::vector<Token> continued = AllTokens("a \\\n b");
    EXPECT_EQ(JoinTokens(continued), "NAME(a) NAME(b) NL EOF");
    EXPECT_EQ(continued[1].line, 2);
    const std::vector<Token> crlf = AllTokens("a \\\r\n b");
    EXPECT_EQ(JoinTokens(crlf), "NAME(a) NAME(b) NL EOF");
    EXPECT_EQ(crlf[1].line, 2);
    // A CR before a newline is a blank, so CRLF programs work.
    EXPECT_EQ(Lex("a\r\n"), "NAME(a) NL EOF");
}

TEST(AwkLexerTest, ImplicitFinalNewline) {
    const std::vector<Token> noNewline = AllTokens("a");
    EXPECT_EQ(JoinTokens(noNewline), "NAME(a) NL EOF");
    EXPECT_EQ(noNewline[1].line, 1);
    EXPECT_EQ(noNewline[1].column, 1u);
    EXPECT_EQ(noNewline[2].line, 1);
    EXPECT_EQ(noNewline[2].column, 1u);

    const std::vector<Token> newline = AllTokens("a\n");
    EXPECT_EQ(JoinTokens(newline), "NAME(a) NL EOF");
    EXPECT_EQ(newline[2].line, 1);
    EXPECT_EQ(newline[2].column, 1u);

    const std::vector<Token> empty = AllTokens("");
    EXPECT_EQ(JoinTokens(empty), "EOF");
    EXPECT_EQ(empty[0].line, 1);
    EXPECT_EQ(empty[0].column, 0u);

    // The added newline is skipped after ; like any other.
    EXPECT_EQ(Lex("a;"), "NAME(a) ; EOF");
}

TEST(AwkLexerTest, Regexes) {
    EXPECT_EQ(LexRegex("/a\\/b[/]c\\.d/"), "ERE(a/b[/]c\\.d) NL EOF");
    EXPECT_EQ(LexRegex("/=x/"), "ERE(=x) NL EOF");
    EXPECT_EQ(LexRegex("/a\\\\/ x"), "ERE(a\\\\) NAME(x) NL EOF");
    EXPECT_EQ(LexRegex("/[]/]/"), "ERE([]/]) NL EOF");
    EXPECT_EQ(LexRegex("/[^]/]x/"), "ERE([^]/]x) NL EOF");
    EXPECT_EQ(LexRegex("/[[:alpha:]/]/"), "ERE([[:alpha:]/]) NL EOF");
    // A '#' inside a regex is a member, not a comment.
    EXPECT_EQ(LexRegex("/#x/"), "ERE(#x) NL EOF");

    // ScanRegex only takes the Slash or DivAssign Next() just returned.
    Lexer lexer(Source("a"));
    const Token name = lexer.Next();
    EXPECT_THROW(lexer.ScanRegex(name), std::logic_error);
}

TEST(AwkLexerTest, Errors) {
    ExpectError("x = \"abc", "unterminated string", 1, 4);
    ExpectError("\"ab\ncd\"", "unterminated string", 1, 0);
    ExpectError("/abc", "unterminated regexp", 1, 1, true);
    ExpectError("/ab\nc/", "unterminated regexp", 1, 1, true);
    ExpectError("a \\ b", "backslash not last character on line", 1, 2);
    // A lone '&' is gawk's syntax error, the caret on it.
    ExpectError("x = 1 & 2", "syntax error", 1, 6);
    ExpectError("a &", "syntax error", 1, 2);
    ExpectError("\n\nx = `", "invalid char '`' in expression", 3, 4);
}

TEST(AwkLexerTest, PositionsAndLineText) {
    Lexer lexer(Source("BEGIN {\n  x = 1 }"));
    const Token begin = lexer.Next();
    EXPECT_EQ(begin.kind, TokenKind::Begin);
    const Token brace = lexer.Next();
    EXPECT_EQ(brace.kind, TokenKind::LeftBrace);
    const Token x = lexer.Next();
    EXPECT_EQ(x.kind, TokenKind::Name);
    EXPECT_EQ(x.text, "x");
    EXPECT_EQ(x.line, 2);
    EXPECT_EQ(x.column, 2u);
    EXPECT_EQ(x.begin, 10u);
    EXPECT_EQ(x.end, 11u);
    EXPECT_EQ(lexer.LineText(2), "  x = 1 }");
    EXPECT_EQ(lexer.LineText(3), "");
    EXPECT_EQ(lexer.LineText(0), "");
}

TEST(AwkLexerTest, FormatsDiagnostics) {
    const AwkSyntaxError error("syntax error", "cmd. line", 1, "\tBEGIN {\tx = = 2 }", 13);
    EXPECT_EQ(Haisos::Awk::FormatAwkSyntaxError(error),
        "awk: cmd. line:1: \tBEGIN {\tx = = 2 }\n"
        "awk: cmd. line:1: \t       \t    ^ syntax error\n");
    const Haisos::Awk::AwkWarning warning{"prog.awk", 3, "m"};
    EXPECT_EQ(Haisos::Awk::FormatAwkWarning(warning), "awk: prog.awk:3: warning: m\n");
    EXPECT_EQ(Haisos::Awk::FormatAwkError("cmd. line", 1, "m"), "awk: cmd. line:1: error: m\n");
}
