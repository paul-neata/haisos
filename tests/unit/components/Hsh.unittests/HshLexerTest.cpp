#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "commands/hsh/HshError.h"
#include "commands/hsh/HshLexer.h"

using namespace Haisos::Hsh;

namespace {

std::vector<Token> LexTokens(const std::string& source, LexerOptions options = {}) {
    Lexer lexer(source, options);
    std::vector<Token> tokens;
    while (true) {
        Token t = lexer.Next();
        bool last = (t.kind == TokenKind::EndOfInput);
        tokens.push_back(std::move(t));
        if (last) {
            break;
        }
    }
    return tokens;
}

std::vector<std::string> Lex(const std::string& source, LexerOptions options = {}) {
    std::vector<std::string> out;
    for (const Token& t : LexTokens(source, options)) {
        out.push_back(DescribeToken(t));
    }
    return out;
}

void ExpectLex(const std::string& source, std::vector<std::string> expected,
               LexerOptions options = {}) {
    expected.push_back("EOF");
    EXPECT_EQ(Lex(source, options), expected) << "source: " << source;
}

void ExpectError(const std::string& source, const std::string& message, int line,
                 bool incomplete, LexerOptions options = {}) {
    Lexer lexer(source, options);
    try {
        while (lexer.Next().kind != TokenKind::EndOfInput) {
        }
        FAIL() << "expected an error for: " << source;
    } catch (const ShellError& e) {
        EXPECT_EQ(std::string(e.what()), message) << source;
        EXPECT_EQ(e.Line(), line) << source;
        EXPECT_EQ(e.Incomplete(), incomplete) << source;
    }
}

} // namespace

TEST(HshLexerTest, OperatorsAndNewlines) {
    ExpectLex("a&&b", {"W(L'a')", "&&", "W(L'b')"});
    ExpectLex("a&>f", {"W(L'a')", "&>", "W(L'f')"});
    ExpectLex("a||b", {"W(L'a')", "||", "W(L'b')"});
    ExpectLex("a|b", {"W(L'a')", "|", "W(L'b')"});
    ExpectLex("a;;b", {"W(L'a')", ";;", "W(L'b')"});
    ExpectLex("a;b\n", {"W(L'a')", ";", "W(L'b')", "NL"});
    ExpectLex("(a)", {"(", "W(L'a')", ")"});
    ExpectLex("a>&2", {"W(L'a')", ">&", "W(L'2')"});
    ExpectLex("a>>f", {"W(L'a')", ">>", "W(L'f')"});
    ExpectLex("a>|f", {"W(L'a')", ">|", "W(L'f')"});
    ExpectLex("a<>f", {"W(L'a')", "<>", "W(L'f')"});
    ExpectLex("a<&f", {"W(L'a')", "<&", "W(L'f')"});
    ExpectLex("a>f", {"W(L'a')", ">", "W(L'f')"});
    ExpectLex("a<f", {"W(L'a')", "<", "W(L'f')"});
    ExpectLex("a&b", {"W(L'a')", "&", "W(L'b')"});
    // Longest match: &>> is &> then >; ;& is ; then &; ;;& is ;; then &.
    ExpectLex("a&>>f", {"W(L'a')", "&>", ">", "W(L'f')"});
    ExpectLex("a;&b", {"W(L'a')", ";", "&", "W(L'b')"});
    ExpectLex("a;;&b", {"W(L'a')", ";;", "&", "W(L'b')"});
    ExpectLex("a<<<w", {"W(L'a')", "<<<", "W(L'w')"});
    ExpectLex("cat <<-E\n\tbody\nE\n", {"W(L'cat')", "<<-", "W(L'E')", "NL"});
    ExpectLex("cat <<E\nbody\nE\n", {"W(L'cat')", "<<", "W(L'E')", "NL"});
    // A line continuation inside an operator.
    ExpectLex("a&\\\n&b", {"W(L'a')", "&&", "W(L'b')"});
    ExpectLex("a |\n| b", {"W(L'a')", "|", "NL", "|", "W(L'b')"});
}

TEST(HshLexerTest, IoNumbers) {
    ExpectLex("2>x", {"IO(2)", ">", "W(L'x')"});
    ExpectLex("0<&3", {"IO(0)", "<&", "W(L'3')"});
    ExpectLex("12>x", {"W(L'12')", ">", "W(L'x')"});
    ExpectLex("a1>x", {"W(L'a1')", ">", "W(L'x')"});
    ExpectLex("2 >x", {"W(L'2')", ">", "W(L'x')"});
    ExpectLex("9>&1", {"IO(9)", ">&", "W(L'1')"});
}

TEST(HshLexerTest, QuotingAndEscapes) {
    ExpectLex("'a b'", {"W(Q'a b')"});
    ExpectLex("\"a $x b\"", {"W(D[Q'a ' P(x) Q' b'])"});
    ExpectLex("\\a", {"W(Q'a')"});
    ExpectLex("\"\\a\\$\"", {"W(D[Q'\\a$'])"});
    ExpectLex("a\"b\"'c'", {"W(L'a' D[Q'b'] Q'c')"});
    ExpectLex("'a''b'", {"W(Q'ab')"});
    ExpectLex("a\\", {"W(L'a' Q'\\')"});
    ExpectLex("'a\\\nb'", {"W(Q'a\\\nb')"});   // kept as written in single quotes
    ExpectLex("\"a\\\nb\"", {"W(D[Q'ab'])"});    // a line continuation
    ExpectLex("''", {"W(Q'')"});
    ExpectLex("\"\"", {"W(D[])"});
    ExpectLex("echo", {"W(L'echo')"});
}

TEST(HshLexerTest, CommentsAndBlanks) {
    ExpectLex("echo # c", {"W(L'echo')"});
    ExpectLex("echo # c\n", {"W(L'echo')", "NL"});
    ExpectLex("a#b", {"W(L'a#b')"});
    ExpectLex("\\#a", {"W(Q'#' L'a')"});
    ExpectLex("a\t b", {"W(L'a')", "W(L'b')"});
    ExpectLex("# only a comment", {});
}

TEST(HshLexerTest, Parameters) {
    ExpectLex("$x", {"W(P(x))"});
    ExpectLex("$ab_1c", {"W(P(ab_1c))"});
    ExpectLex("$10", {"W(P(1) L'0')"});
    ExpectLex("$@", {"W(P(@))"});
    ExpectLex("$*", {"W(P(*))"});
    ExpectLex("$#", {"W(P(#))"});
    ExpectLex("$?", {"W(P(?))"});
    ExpectLex("$-", {"W(P(-))"});
    ExpectLex("$$", {"W(P($))"});
    ExpectLex("$!", {"W(P(!))"});
    ExpectLex("$0", {"W(P(0))"});
    ExpectLex("$", {"W(L'$')"});
    ExpectLex("$'a'", {"W(L'$' Q'a')"});
    ExpectLex("$\"a\"", {"W(L'$' D[Q'a'])"});
    ExpectLex("${x}", {"W(P(x))"});
    ExpectLex("${10}", {"W(P(10))"});
    ExpectLex("${#x}", {"W(P(#x))"});
    ExpectLex("${#}", {"W(P(#))"});
    ExpectLex("${x:-w}", {"W(P(x:-[L'w']))"});
    ExpectLex("${x-w}", {"W(P(x-[L'w']))"});
    ExpectLex("${x:=w}", {"W(P(x:=[L'w']))"});
    ExpectLex("${x=w}", {"W(P(x=[L'w']))"});
    ExpectLex("${x:?w}", {"W(P(x:?[L'w']))"});
    ExpectLex("${x?w}", {"W(P(x?[L'w']))"});
    ExpectLex("${x:+w}", {"W(P(x:+[L'w']))"});
    ExpectLex("${x+w}", {"W(P(x+[L'w']))"});
    ExpectLex("${x%w}", {"W(P(x%[L'w']))"});
    ExpectLex("${x%%w}", {"W(P(x%%[L'w']))"});
    ExpectLex("${x#w}", {"W(P(x#[L'w']))"});
    ExpectLex("${x##w}", {"W(P(x##[L'w']))"});
    ExpectLex("${x-}", {"W(P(x-[]))"});
    ExpectLex("${x:-${y:-z}}", {"W(P(x:-[P(y:-[L'z'])]))"});
    ExpectLex("${x-\\}}", {"W(P(x-[Q'}']))"});
    ExpectLex("\"${x:-\"a b\"}\"", {"W(D[P(x:-[D[Q'a b']])])"});
    ExpectLex("\"${x:-'a'}\"", {"W(D[P(x:-[Q''a''])])"});
    // A pattern operand keeps its own quotes even inside double quotes;
    // everywhere else in a double-quoted operand a ' is a plain character.
    ExpectLex("\"${x#'a'}\"", {"W(D[P(x#[Q'a'])])"});
    ExpectLex("\"${x%%'a'}\"", {"W(D[P(x%%[Q'a'])])"});
    ExpectLex("${}", {"W(P(<bad>))"});
    ExpectLex("${x!}", {"W(P(<bad>))"});
    ExpectLex("${#x:-a}", {"W(P(<bad>))"});
    ExpectLex("${x:2:3}", {"W(P(<bad>))"});
    ExpectLex("${#:-a}", {"W(P(#:-[L'a']))"}); // '#' with no name after it is $# itself
    ExpectLex("${x:}", {"W(P(<bad>))"}); // dash says "Missing '}': a documented deviation
    // Merging happens in operands too, at every level.
    ExpectLex("${x:-a'b'c}", {"W(P(x:-[L'a' Q'b' L'c']))"});
    ExpectLex("${x:-'a''b'}", {"W(P(x:-[Q'ab']))"});
    ExpectLex("x${x}y", {"W(L'x' P(x) L'y')"});
}

TEST(HshLexerTest, CommandSubstitutionEnds) {
    ExpectLex("$(echo \")\")", {"W(C'echo \")\"')"});
    ExpectLex("$( (echo a) )", {"W(C' (echo a) ')"});
    ExpectLex("$(case a in a) echo m;; esac)", {"W(C'case a in a) echo m;; esac')"});
    ExpectLex("$(case a in (a) echo;; esac) x", {"W(C'case a in (a) echo;; esac')", "W(L'x')"});
    ExpectLex("$(echo a # c\n)", {"W(C'echo a # c\n')"});
    ExpectLex("x=$(echo \"a)\"; echo 'b)' \\))",
              {"W(L'x=' C'echo \"a)\"; echo 'b)' \\)')"});
    ExpectLex("$(echo $(echo a))", {"W(C'echo $(echo a)')"});
    ExpectLex("$(echo a) y", {"W(C'echo a')", "W(L'y')"});
    // A heredoc inside a command substitution: the text holds the body.
    ExpectLex("$(cat <<E\nin\nE\n)", {"W(C'cat <<E\nin\nE\n')"});
    // A heredoc pending at the closing ')' gets an empty body.
    {
        std::vector<Token> tokens = LexTokens("$(cat <<E)");
        ASSERT_EQ(tokens.size(), 2u);
        ASSERT_EQ(tokens[0].kind, TokenKind::Word);
        ASSERT_EQ(tokens[0].word.parts.size(), 1u);
        EXPECT_EQ(tokens[0].word.parts[0].text, "cat <<E");
    }
}

TEST(HshLexerTest, Backquotes) {
    ExpectLex("`echo \\`echo hi\\``", {"W(B'echo `echo hi`')"});
    ExpectLex("\"`echo \\\"q\\\"`\"", {"W(D[B'echo \"q\"'])"});
    ExpectLex("`echo \\$x`", {"W(B'echo $x')"});
    ExpectLex("`echo a` b", {"W(B'echo a')", "W(L'b')"});
}

TEST(HshLexerTest, Arithmetic) {
    ExpectLex("$((1+2))", {"W(A[L'1+2'])"});
    ExpectLex("$(( (3) + (4) ))", {"W(A[L' (3) + (4) '])"});
    ExpectLex("$((1 + $(echo 2)))", {"W(A[L'1 + ' C'echo 2'])"});
    ExpectLex("$((1+$y))", {"W(A[L'1+' P(y)])"});
    ExpectLex("$((x))a", {"W(A[L'x'] L'a')"});
    ExpectLex("$((1 +\n2))", {"W(A[L'1 +\n2'])"});
    // Quotes are plain characters in arithmetic, as dash.
    ExpectLex("$((a'b'))", {"W(A[L'a'b''])"});
}

TEST(HshLexerTest, HereDocuments) {
    // A plain heredoc: body, parts, offsets.
    {
        std::vector<Token> tokens = LexTokens("cat <<E\nbody\nE\n");
        ASSERT_EQ(tokens.size(), 5u);
        ASSERT_EQ(tokens[2].kind, TokenKind::Word);
        std::shared_ptr<HereDocument> hd = tokens[2].hereDoc;
        ASSERT_TRUE(hd != nullptr);
        EXPECT_EQ(hd->delimiter, "E");
        EXPECT_FALSE(hd->quoted);
        EXPECT_FALSE(hd->stripTabs);
        EXPECT_EQ(hd->rawBody, "body\n");
        EXPECT_TRUE(hd->complete);
        EXPECT_TRUE(hd->terminated);
        ASSERT_EQ(hd->body.parts.size(), 1u);
        EXPECT_EQ(hd->body.parts[0].kind, WordPartKind::Quoted);
        EXPECT_EQ(hd->body.parts[0].text, "body\n");
        EXPECT_EQ(tokens[3].kind, TokenKind::Newline);
        EXPECT_EQ(tokens[4].kind, TokenKind::EndOfInput);
    }
    // <<-: leading tabs stripped from body and delimiter lines.
    {
        std::vector<Token> tokens = LexTokens("cat <<-E\n\tbody\n\t\tmore\n\tE\n");
        ASSERT_TRUE(tokens[2].hereDoc != nullptr);
        const HereDocument& hd = *tokens[2].hereDoc;
        EXPECT_TRUE(hd.stripTabs);
        EXPECT_FALSE(hd.quoted);
        EXPECT_EQ(hd.rawBody, "body\nmore\n"); // every leading tab of a line is removed
        EXPECT_TRUE(hd.terminated);
    }
    // Quoted delimiters: no expansion, body as one Quoted part.
    for (const char* op : {"<<'E'", "<<\"E\"", "<<\\E"}) {
        std::vector<Token> tokens = LexTokens(std::string("cat ") + op + "\n$x `y`\nE\n");
        ASSERT_TRUE(tokens[2].hereDoc != nullptr) << op;
        const HereDocument& hd = *tokens[2].hereDoc;
        EXPECT_TRUE(hd.quoted) << op;
        EXPECT_EQ(hd.delimiter, "E") << op;
        EXPECT_EQ(hd.rawBody, "$x `y`\n") << op;
        ASSERT_EQ(hd.body.parts.size(), 1u);
        EXPECT_EQ(hd.body.parts[0].kind, WordPartKind::Quoted);
        EXPECT_EQ(hd.body.parts[0].text, "$x `y`\n");
    }
    // A quoted delimiter can be combined: <<E"F" has the delimiter EF.
    {
        std::vector<Token> tokens = LexTokens("cat <<E\"F\"\n$x\nEF\n");
        ASSERT_TRUE(tokens[2].hereDoc != nullptr);
        const HereDocument& hd = *tokens[2].hereDoc;
        EXPECT_TRUE(hd.quoted);
        EXPECT_EQ(hd.delimiter, "EF");
        EXPECT_EQ(hd.rawBody, "$x\n");
        EXPECT_TRUE(hd.terminated);
    }
    // An unquoted delimiter made of a word: <<$x keeps "$x" as the delimiter.
    {
        std::vector<Token> tokens = LexTokens("cat <<$x\nno\n$x\n");
        ASSERT_TRUE(tokens[2].hereDoc != nullptr);
        const HereDocument& hd = *tokens[2].hereDoc;
        EXPECT_FALSE(hd.quoted);
        EXPECT_EQ(hd.delimiter, "$x");
        EXPECT_EQ(hd.rawBody, "no\n");
        EXPECT_TRUE(hd.terminated);
    }
    // Expansions, escapes and continuations in an unquoted body.
    {
        std::string source = "cat <<E\n$x\n\\$x\na\\\nb\n$(echo cs)\nE\n";
        std::vector<Token> tokens = LexTokens(source);
        ASSERT_TRUE(tokens[2].hereDoc != nullptr);
        const HereDocument& hd = *tokens[2].hereDoc;
        EXPECT_TRUE(hd.terminated);
        EXPECT_EQ(DescribeWord(hd.body), "P(x) Q'\n$x\nab\n' C'echo cs' Q'\n'");
    }
    // A ${...} operand in a body is lexed as inside double quotes: ' is a plain character.
    {
        std::vector<Token> tokens = LexTokens("cat <<E\n${x:-it's}\nE\n");
        ASSERT_TRUE(tokens[2].hereDoc != nullptr);
        EXPECT_EQ(DescribeWord(tokens[2].hereDoc->body), "P(x:-[Q'it's']) Q'\n'");
    }
    // Several heredocs on one line are read one after the other.
    {
        std::vector<Token> tokens = LexTokens("cat <<A; cat <<B\nboA\nA\nboB\nB\nz\n");
        ASSERT_EQ(tokens.size(), 11u);
        ASSERT_TRUE(tokens[2].hereDoc != nullptr && tokens[6].hereDoc != nullptr);
        EXPECT_EQ(tokens[2].hereDoc->rawBody, "boA\n");
        EXPECT_EQ(tokens[2].hereDoc->delimiter, "A");
        EXPECT_EQ(tokens[6].hereDoc->rawBody, "boB\n");
        EXPECT_EQ(tokens[6].hereDoc->delimiter, "B");
        EXPECT_EQ(tokens[8].kind, TokenKind::Word);
        EXPECT_EQ(DescribeWord(tokens[8].word), "L'z'");
        EXPECT_EQ(tokens[8].line, 6);
    }
    // The body not terminated at the end of input: complete, not terminated, no error.
    {
        std::vector<Token> tokens = LexTokens("cat <<E\nbody\n");
        ASSERT_TRUE(tokens[2].hereDoc != nullptr);
        const HereDocument& hd = *tokens[2].hereDoc;
        EXPECT_EQ(hd.rawBody, "body\n");
        EXPECT_TRUE(hd.complete);
        EXPECT_FALSE(hd.terminated);
        EXPECT_EQ(tokens.back().kind, TokenKind::EndOfInput);
    }
    // A quoted heredoc with an empty body keeps no part.
    {
        std::vector<Token> tokens = LexTokens("cat <<'E'\nE\n");
        ASSERT_TRUE(tokens[2].hereDoc != nullptr);
        EXPECT_TRUE(tokens[2].hereDoc->body.parts.empty());
    }
}

TEST(HshLexerTest, TokenOffsets) {
    const std::string source = "  echo \"a b\"$(x) 2>&1 &\\\n& y";
    std::vector<Token> tokens = LexTokens(source);
    ASSERT_EQ(tokens.size(), 8u);
    const std::vector<std::string> texts = {"echo", "\"a b\"$(x)", "2", ">&", "1",
                                            "&\\\n&", "y", ""};
    const std::vector<std::string> kinds = {"W(L'echo')", "W(D[Q'a b'] C'x')", "IO(2)",
                                            ">&", "W(L'1')", "&&", "W(L'y')", "EOF"};
    for (size_t i = 0; i < tokens.size(); ++i) {
        EXPECT_EQ(DescribeToken(tokens[i]), kinds[i]) << i;
        EXPECT_EQ(source.substr(tokens[i].begin, tokens[i].end - tokens[i].begin), texts[i]) << i;
    }
    EXPECT_EQ(tokens[7].begin, source.size());
    EXPECT_EQ(tokens[7].end, source.size());
}

TEST(HshLexerTest, HereDocumentOffsets) {
    {
        const std::string source = "cat <<E; echo\nbody\nE\nnext";
        std::vector<Token> tokens = LexTokens(source);
        ASSERT_TRUE(tokens[2].hereDoc != nullptr);
        const HereDocument& hd = *tokens[2].hereDoc;
        EXPECT_EQ(source.substr(hd.sourceBegin, hd.sourceEnd - hd.sourceBegin), "body\nE\n");
        EXPECT_EQ(tokens.back().begin, source.size());
    }
    {
        // Not terminated: the region runs to the end of the input.
        const std::string source = "cat <<E; echo\nbody\nno stop";
        std::vector<Token> tokens = LexTokens(source);
        ASSERT_TRUE(tokens[2].hereDoc != nullptr);
        const HereDocument& hd = *tokens[2].hereDoc;
        EXPECT_EQ(source.substr(hd.sourceBegin, hd.sourceEnd - hd.sourceBegin), "body\nno stop");
        EXPECT_EQ(hd.sourceEnd, source.size());
        EXPECT_EQ(hd.rawBody, "body\nno stop");
        EXPECT_FALSE(hd.terminated);
    }
}

TEST(HshLexerTest, LineNumbers) {
    {
        std::vector<Token> tokens = LexTokens("a\nb c\nd");
        ASSERT_EQ(tokens.size(), 7u);
        EXPECT_EQ(tokens[0].line, 1); // a
        EXPECT_EQ(tokens[1].line, 1); // NL
        EXPECT_EQ(tokens[2].line, 2); // b
        EXPECT_EQ(tokens[3].line, 2); // c
        EXPECT_EQ(tokens[4].line, 2); // NL
        EXPECT_EQ(tokens[5].line, 3); // d
        EXPECT_EQ(tokens[0].word.line, 1);
    }
    {
        // A continuation and a heredoc body move the line.
        // line 1: "a \"  line 2: "b; cat <<E"  3: body  4: E  5: z
        std::vector<Token> tokens = LexTokens("a \\\nb; cat <<E\nbody\nE\nz");
        ASSERT_EQ(tokens.size(), 9u);
        EXPECT_EQ(tokens[0].line, 1); // a
        EXPECT_EQ(tokens[1].line, 2); // b
        EXPECT_EQ(tokens[2].line, 2); // ;
        EXPECT_EQ(tokens[4].line, 2); // <<
        EXPECT_EQ(tokens[5].line, 2); // E
        EXPECT_EQ(tokens[6].line, 2); // NL
        EXPECT_EQ(tokens[7].line, 5); // z, after the two body lines
        EXPECT_EQ(tokens[8].line, 5); // EOF
    }
    {
        LexerOptions options;
        options.firstLine = 5;
        std::vector<Token> tokens = LexTokens("a\nb", options);
        EXPECT_EQ(tokens[0].line, 5);
        EXPECT_EQ(tokens[1].line, 5);
        EXPECT_EQ(tokens[2].line, 6);
    }
}

TEST(HshLexerTest, Errors) {
    ExpectError("echo 'a", "Syntax error: Unterminated quoted string", 1, true);
    ExpectError("echo \"a", "Syntax error: Unterminated quoted string", 1, true);
    ExpectError("echo \"a\nb", "Syntax error: Unterminated quoted string", 2, true);
    ExpectError("${x", "Syntax error: Missing '}'", 1, true);
    ExpectError("${x:-a\nb", "Syntax error: Missing '}'", 2, true);
    ExpectError("${#", "Syntax error: Missing '}'", 1, true);
    ExpectError("$((1+2", "Syntax error: Missing '))'", 1, true);
    ExpectError("$((echo a) )", "Syntax error: Missing '))'", 1, false);
    ExpectError("`abc", "Syntax error: EOF in backquote substitution", 1, true);
    ExpectError("$(echo", "Syntax error: end of file unexpected (expecting \")\")", 1, true);
    ExpectError("$(echo a\nb", "Syntax error: end of file unexpected (expecting \")\")", 2, true);
}

TEST(HshLexerTest, InteractiveIncomplete) {
    LexerOptions interactive;
    interactive.interactive = true;
    // A heredoc whose delimiter line has not come yet.
    ExpectError("cat <<E\nx\n", "Syntax error: end of file unexpected", 3, true, interactive);
    ExpectError("cat <<E", "Syntax error: end of file unexpected", 1, true, interactive);
    // A source ending in a backslash-newline.
    ExpectError("echo a\\\n", "Syntax error: end of file unexpected", 2, true, interactive);
    // A complete heredoc is fine.
    ExpectLex("cat <<E\nx\nE\n", {"W(L'cat')", "<<", "W(L'E')", "NL"}, interactive);
    // Without it, both lex fine.
    ExpectLex("cat <<E\nx\n", {"W(L'cat')", "<<", "W(L'E')", "NL"});
    ExpectLex("echo a\\\n", {"W(L'echo')", "W(L'a')"});
}

TEST(HshLexerTest, ReservedWordsAndNames) {
    const ReservedWord all[] = {ReservedWord::If,    ReservedWord::Then, ReservedWord::Else,
                                ReservedWord::Elif,  ReservedWord::Fi,   ReservedWord::Do,
                                ReservedWord::Done,  ReservedWord::Case, ReservedWord::Esac,
                                ReservedWord::While, ReservedWord::Until, ReservedWord::For,
                                ReservedWord::In,    ReservedWord::LeftBrace,
                                ReservedWord::RightBrace, ReservedWord::Bang};
    for (ReservedWord word : all) {
        std::vector<Token> tokens = LexTokens(ReservedWordText(word));
        ASSERT_EQ(tokens[0].kind, TokenKind::Word);
        std::optional<ReservedWord> as = AsReservedWord(tokens[0]);
        ASSERT_TRUE(as.has_value()) << ReservedWordText(word);
        EXPECT_EQ(*as, word);
        EXPECT_STREQ(ReservedWordText(*as), ReservedWordText(word));
    }
    // Quoted or longer words are not reserved words.
    for (const char* source : {"\"if\"", "\\if", "ifx", "iffy", "{}", "!!"}) {
        std::vector<Token> tokens = LexTokens(source);
        EXPECT_FALSE(AsReservedWord(tokens[0]).has_value()) << source;
    }
    EXPECT_TRUE(IsValidShellName("a"));
    EXPECT_TRUE(IsValidShellName("_x9"));
    EXPECT_TRUE(IsValidShellName("A1_b"));
    EXPECT_FALSE(IsValidShellName(""));
    EXPECT_FALSE(IsValidShellName("1a"));
    EXPECT_FALSE(IsValidShellName("a-b"));
    // LiteralText: only unquoted literal text.
    {
        std::vector<Token> tokens = LexTokens("abc");
        EXPECT_EQ(LiteralText(tokens[0].word), std::optional<std::string>("abc"));
    }
    for (const char* source : {"a'b'", "a$b", "\"a\"", "a\\b"}) {
        EXPECT_FALSE(LiteralText(LexTokens(source)[0].word).has_value()) << source;
    }
}

TEST(HshLexerTest, FormatShellError) {
    EXPECT_EQ(FormatShellError("hsh", 3, "x"), "hsh: 3: x\n");
    EXPECT_EQ(FormatShellError("hsh", 1, "Syntax error: Unterminated quoted string"),
              "hsh: 1: Syntax error: Unterminated quoted string\n");
    ShellError error("boom", 7, true);
    EXPECT_EQ(error.Line(), 7);
    EXPECT_TRUE(error.Incomplete());
    EXPECT_EQ(std::string(error.what()), "boom");
}

TEST(HshLexerTest, OperatorAndReservedWordText) {
    EXPECT_STREQ(OperatorText(TokenKind::AndIf), "&&");
    EXPECT_STREQ(OperatorText(TokenKind::DoubleLessDash), "<<-");
    EXPECT_STREQ(OperatorText(TokenKind::TripleLess), "<<<");
    EXPECT_STREQ(OperatorText(TokenKind::AndGreat), "&>");
    EXPECT_STREQ(OperatorText(TokenKind::Word), "");
    EXPECT_STREQ(OperatorText(TokenKind::Newline), "");
    EXPECT_TRUE(IsRedirectionOperator(TokenKind::Less));
    EXPECT_TRUE(IsRedirectionOperator(TokenKind::DoubleLess));
    EXPECT_TRUE(IsRedirectionOperator(TokenKind::AndGreat));
    EXPECT_FALSE(IsRedirectionOperator(TokenKind::Semicolon));
    EXPECT_FALSE(IsRedirectionOperator(TokenKind::Pipe));
    // DescribeToken of an operator is its text; the other kinds have shapes.
    EXPECT_EQ(DescribeToken(LexTokens(";")[0]), ";");
}

TEST(HshLexerTest, LexerIsMovable) {
    Lexer lexer("a | b\n", {});
    EXPECT_EQ(DescribeToken(lexer.Next()), "W(L'a')");
    EXPECT_EQ(DescribeToken(lexer.Next()), "|");
    Lexer moved(std::move(lexer));  // move-construct mid-stream
    EXPECT_EQ(DescribeToken(moved.Next()), "W(L'b')");
    Lexer assigned("x", {});
    assigned = std::move(moved);    // move-assign mid-stream
    EXPECT_EQ(DescribeToken(assigned.Next()), "NL");
    EXPECT_EQ(DescribeToken(assigned.Next()), "EOF");
}

TEST(HshLexerTest, HereDocLineContinuation) {
    // Unquoted delimiter: a \<newline> inside the body joins with the next
    // line before the delimiter comparison, so this body's first logical line
    // is "aE" and the delimiter matches on the third body line.
    {
        std::vector<Token> tokens = LexTokens("cat <<E\na\\\nE\nE\n");
        ASSERT_EQ(tokens.size(), 5u);
        std::shared_ptr<HereDocument> hd = tokens[2].hereDoc;
        ASSERT_TRUE(hd != nullptr);
        EXPECT_EQ(hd->rawBody, "aE\n");
        EXPECT_TRUE(hd->terminated);
        ASSERT_EQ(hd->body.parts.size(), 1u);
        EXPECT_EQ(hd->body.parts[0].text, "aE\n");
        EXPECT_EQ(tokens[4].line, 5);
    }
    // Quoted delimiter: raw-line matching, no continuation handling.
    {
        std::vector<Token> tokens = LexTokens("cat <<'E'\na\\\nE\nnext\n");
        std::shared_ptr<HereDocument> hd = tokens[2].hereDoc;
        ASSERT_TRUE(hd != nullptr);
        EXPECT_EQ(hd->rawBody, "a\\\n");
        EXPECT_TRUE(hd->terminated);
        EXPECT_EQ(tokens[3].kind, TokenKind::Newline);
        EXPECT_EQ(tokens[4].kind, TokenKind::Word); // "next" is a command now
        EXPECT_EQ(tokens[4].line, 4);
    }
}
