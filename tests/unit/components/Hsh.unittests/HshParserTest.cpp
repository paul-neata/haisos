#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "commands/hsh/HshAst.h"
#include "commands/hsh/HshError.h"
#include "commands/hsh/HshParser.h"

using namespace Haisos::Hsh;

namespace {

void ExpectDump(const std::string& source, const std::string& expected,
                ParserOptions options = {}) {
    ParseResult result = ParseProgram(source, options);
    ASSERT_EQ(result.status, ParseResult::Status::Command) << "source: " << source
        << " error: " << result.errorMessage << " (line " << result.errorLine << ")";
    EXPECT_EQ(DumpCommandList(result.commands), expected) << "source: " << source;
}

void ExpectError(const std::string& source, const std::string& message, int line,
                 bool incomplete, ParserOptions options = {}) {
    ParseResult result = ParseProgram(source, options);
    ASSERT_EQ(result.status, ParseResult::Status::Error) << "source: " << source;
    EXPECT_EQ(result.errorMessage, message) << "source: " << source;
    EXPECT_EQ(result.errorLine, line) << "source: " << source;
    EXPECT_EQ(result.incomplete, incomplete) << "source: " << source;
}

// The one command of a one-item, one-pipeline parse.
const Command& SingleCommand(const CommandList& list) {
    EXPECT_EQ(list.items.size(), 1u);
    EXPECT_EQ(list.items[0].andOr.pipelines.size(), 1u);
    EXPECT_EQ(list.items[0].andOr.pipelines[0].commands.size(), 1u);
    return *list.items[0].andOr.pipelines[0].commands[0];
}

const SimpleCommand& SingleSimple(const CommandList& list) {
    const Command& command = SingleCommand(list);
    EXPECT_EQ(command.kind, CommandKind::Simple);
    return static_cast<const SimpleCommand&>(command);
}

ParseResult MustParse(const std::string& source) {
    ParseResult result = ParseProgram(source);
    EXPECT_EQ(result.status, ParseResult::Status::Command) << source << ": " << result.errorMessage;
    return result;
}

// Every list item and every function definition, top level and nested.
void CollectItems(const CommandList& list, std::vector<const ListItem*>& items,
                  std::vector<const FunctionDefinition*>& functions);

void CollectFromCommand(const Command& command, std::vector<const ListItem*>& items,
                        std::vector<const FunctionDefinition*>& functions) {
    switch (command.kind) {
        case CommandKind::BraceGroup:
        case CommandKind::Subshell: {
            const CommandList& body = command.kind == CommandKind::BraceGroup
                ? static_cast<const BraceGroup&>(command).body
                : static_cast<const Subshell&>(command).body;
            CollectItems(body, items, functions);
            break;
        }
        case CommandKind::If: {
            const auto& ifCommand = static_cast<const IfCommand&>(command);
            for (const IfBranch& branch : ifCommand.branches) {
                CollectItems(branch.condition, items, functions);
                CollectItems(branch.body, items, functions);
            }
            if (ifCommand.elseBody) {
                CollectItems(*ifCommand.elseBody, items, functions);
            }
            break;
        }
        case CommandKind::While:
        case CommandKind::Until: {
            const auto& loop = static_cast<const LoopCommand&>(command);
            CollectItems(loop.condition, items, functions);
            CollectItems(loop.body, items, functions);
            break;
        }
        case CommandKind::For:
            CollectItems(static_cast<const ForCommand&>(command).body, items, functions);
            break;
        case CommandKind::Case:
            for (const CaseItem& item : static_cast<const CaseCommand&>(command).items) {
                CollectItems(item.body, items, functions);
            }
            break;
        case CommandKind::FunctionDefinition: {
            const auto& function = static_cast<const FunctionDefinition&>(command);
            functions.push_back(&function);
            if (function.body) {
                CollectFromCommand(*function.body, items, functions);
            }
            break;
        }
        case CommandKind::Simple:
            break;
    }
}

void CollectItems(const CommandList& list, std::vector<const ListItem*>& items,
                  std::vector<const FunctionDefinition*>& functions) {
    for (const ListItem& item : list.items) {
        items.push_back(&item);
        for (const Pipeline& pipeline : item.andOr.pipelines) {
            for (const CommandPtr& command : pipeline.commands) {
                CollectFromCommand(*command, items, functions);
            }
        }
    }
}

std::string DumpSingleItem(const ListItem& item) {
    CommandList list;
    list.items.push_back(item);
    return DumpCommandList(list);
}

// Every sourceText re-parses on its own to the same command.
void ExpectSourceTextsRoundTrip(const std::string& source) {
    ParseResult result = MustParse(source);
    std::vector<const ListItem*> items;
    std::vector<const FunctionDefinition*> functions;
    CollectItems(result.commands, items, functions);
    for (const ListItem* item : items) {
        ParseResult again = ParseProgram(item->sourceText);
        ASSERT_EQ(again.status, ParseResult::Status::Command)
            << "sourceText: " << item->sourceText << " error: " << again.errorMessage;
        ASSERT_EQ(again.commands.items.size(), 1u) << "sourceText: " << item->sourceText;
        ListItem reparsed = again.commands.items.front();
        reparsed.background = item->background;  // the & (or its absence) is the item's end
        EXPECT_EQ(DumpSingleItem(reparsed), DumpSingleItem(*item))
            << "source: " << source << " sourceText: " << item->sourceText;
    }
    for (const FunctionDefinition* function : functions) {
        ParseResult again = ParseProgram(function->sourceText);
        ASSERT_EQ(again.status, ParseResult::Status::Command)
            << "sourceText: " << function->sourceText;
        ASSERT_EQ(again.commands.items.size(), 1u) << function->sourceText;
        const Command& body = SingleCommand(again.commands);
        ASSERT_EQ(body.kind, CommandKind::FunctionDefinition) << function->sourceText;
        EXPECT_EQ(DumpCommand(body), DumpCommand(*function))
            << "source: " << source << " sourceText: " << function->sourceText;
    }
}

} // namespace

TEST(HshParserTest, SimpleCommands) {
    ExpectDump("echo a b", "[echo a b]");
    ExpectDump("x=1 y=2 echo a", "[x=1 y=2 echo a]");
    ExpectDump("echo a=b", "[echo a=b]");   // a=b after the command name is a word
    ExpectDump("echo a=b=1", "[echo a=b=1]");
    ExpectDump("x=1", "[x=1]");
    ExpectDump("x=", "[x=]");
    ExpectDump(">f", "[1>f]");
    ExpectDump("<f echo hi", "[echo hi 0<f]");
    ExpectDump("echo a >f <g", "[echo a 1>f 0<g]");
    ExpectDump("echo >f a", "[echo a 1>f]");
    // Every redirection kind, with and without an IO number.
    ExpectDump("c <f", "[c 0<f]");
    ExpectDump("c 3<f", "[c 3<f]");
    ExpectDump("c >f", "[c 1>f]");
    ExpectDump("c 2>f", "[c 2>f]");
    ExpectDump("c >|f", "[c 1>|f]");
    ExpectDump("c >>f", "[c 1>>f]");
    ExpectDump("c <>f", "[c 0<>f]");
    ExpectDump("c <&3", "[c 0<&3]");
    ExpectDump("c >&2", "[c 1>&2]");
    ExpectDump("c 2>&1", "[c 2>&1]");
    ExpectDump("c >&-", "[c 1>&-]");
    ExpectDump("c <&-", "[c 0<&-]");
    ExpectDump("c <<<w", "[c 0<<<w]");
    ExpectDump("c 0<<<w", "[c 0<<<w]");
    ExpectDump("c &>f", "[c &>f]");
    ExpectDump("c >&\"1\"", "[c 1>&\"1\"]"); // quoted digit: fine
    // Shapes of the nodes.
    {
        ParseResult result = MustParse("x=1 echo $v >out 2>&1");
        const SimpleCommand& simple = SingleSimple(result.commands);
        ASSERT_EQ(simple.assignments.size(), 1u);
        EXPECT_EQ(simple.assignments[0].name, "x");
        EXPECT_EQ(simple.assignments[0].value.source, "1");
        ASSERT_EQ(simple.words.size(), 2u);
        EXPECT_EQ(simple.words[0].source, "echo");
        EXPECT_EQ(simple.words[1].source, "$v");
        ASSERT_EQ(simple.redirections.size(), 2u);
        EXPECT_EQ(simple.redirections[0].kind, RedirectionKind::Output);
        EXPECT_EQ(simple.redirections[0].fd, 1);
        EXPECT_EQ(simple.redirections[0].target.source, "out");
        EXPECT_EQ(simple.redirections[1].kind, RedirectionKind::DupOutput);
        EXPECT_EQ(simple.redirections[1].fd, 2);
        EXPECT_EQ(simple.redirections[1].target.source, "1");
    }
    {
        // A dup target with an expansion part is not checked here (`>&$fd`).
        ParseResult result = MustParse("c >&$fd");
        const SimpleCommand& simple = SingleSimple(result.commands);
        ASSERT_EQ(simple.redirections.size(), 1u);
        EXPECT_EQ(simple.redirections[0].kind, RedirectionKind::DupOutput);
    }
    {
        ParseResult result = MustParse("c 8<>f 9>>g");
        const SimpleCommand& simple = SingleSimple(result.commands);
        ASSERT_EQ(simple.redirections.size(), 2u);
        EXPECT_EQ(simple.redirections[0].kind, RedirectionKind::ReadWrite);
        EXPECT_EQ(simple.redirections[0].fd, 8);
        EXPECT_EQ(simple.redirections[1].kind, RedirectionKind::Append);
        EXPECT_EQ(simple.redirections[1].fd, 9);
    }
}

TEST(HshParserTest, PipelinesAndLists) {
    ExpectDump("a | b | c", "[a] | [b] | [c]");
    ExpectDump("! a | b", "! [a] | [b]");
    ExpectDump("a && b || c", "[a] && [b] || [c]");
    ExpectDump("a & b; c", "[a] &; [b]; [c]");
    ExpectDump("a;", "[a]");
    ExpectDump("a &", "[a] &");
    ExpectDump("a | b && ! c &", "[a] | [b] && ! [c] &");
    // Newlines after &&, || and | are skipped.
    ExpectDump("a &&\nb", "[a] && [b]");
    ExpectDump("a ||\nb", "[a] || [b]");
    ExpectDump("a |\nb | c", "[a] | [b] | [c]");
    ExpectDump("a &&\n\n\nb", "[a] && [b]");
    {
        ParseResult result = MustParse("! a | b &");
        const ListItem& item = result.commands.items[0];
        EXPECT_TRUE(item.background);
        EXPECT_TRUE(item.andOr.pipelines[0].negated);
        EXPECT_EQ(item.andOr.pipelines[0].commands.size(), 2u);
    }
}

TEST(HshParserTest, CompoundCommands) {
    ExpectDump("{ a; b; }", "{ [a]; [b] }");
    ExpectDump("( a; b )", "( [a]; [b] )");
    ExpectDump("{ ( a ); { b; }; }", "{ ( [a] ); { [b] } }");
    ExpectDump("{\na\nb\n}", "{ [a]; [b] }");
    ExpectDump("if a; then b; fi", "if [a] then [b] fi");
    ExpectDump("if a\nthen b\nelif c\nthen d\nelse e\nfi\n",
               "if [a] then [b] elif [c] then [d] else [e] fi");
    ExpectDump("if a; then b; elif c; then d; fi", "if [a] then [b] elif [c] then [d] fi");
    ExpectDump("while a; do b; done", "while [a] do [b] done");
    ExpectDump("until a; do b; done", "until [a] do [b] done");
    ExpectDump("for x in a b; do echo $x; done", "for x in a b do [echo $x] done");
    ExpectDump("for x in; do :; done", "for x in do [:] done");
    ExpectDump("for x in ; do :; done", "for x in do [:] done");
    ExpectDump("for x; do :; done", "for x do [:] done");
    ExpectDump("for x\ndo :; done", "for x do [:] done");
    ExpectDump("for x in a b\ndo :; done", "for x in a b do [:] done");
    ExpectDump("case x in (a) echo;; esac", "case x in a) [echo] ;; esac");
    ExpectDump("case x in a|b) echo;; esac", "case x in a|b) [echo] ;; esac");
    ExpectDump("case $x in a|b) echo;; *) ;; esac", "case $x in a|b) [echo] ;; *) ;; esac");
    ExpectDump("case x in a) ;; b) ;; esac", "case x in a) ;; b) ;; esac");
    ExpectDump("case x in a) ;; b) esac", "case x in a) ;; b) ;; esac"); // the last item's ;; is optional
    ExpectDump("case x in a) echo m; esac", "case x in a) [echo m] ;; esac");
    ExpectDump("case x in a) echo m\nesac", "case x in a) [echo m] ;; esac");
    ExpectDump("case x in esac", "case x in esac");
    ExpectDump("case in in in) echo m;; esac", "case in in in) [echo m] ;; esac");
    ExpectDump("case x in\na) echo m;;\nesac", "case x in a) [echo m] ;; esac");
    // Redirections after compound commands.
    ExpectDump("{ a; } >f", "{ [a] } 1>f");
    ExpectDump("( a ) 2>&1", "( [a] ) 2>&1");
    ExpectDump("if a; then b; fi <in >out", "if [a] then [b] fi 0<in 1>out");
    ExpectDump("while a; do b; done >f", "while [a] do [b] done 1>f");
    ExpectDump("for x in a; do b; done >f", "for x in a do [b] done 1>f");
    ExpectDump("case x in a) b;; esac >f", "case x in a) [b] ;; esac 1>f");
}

TEST(HshParserTest, Functions) {
    ExpectDump("f() { echo; }", "f() { [echo] }");
    ExpectDump("f()\n{ echo; }", "f() { [echo] }");
    ExpectDump("f() echo hi", "f() [echo hi]");
    ExpectDump("f() ( echo )", "f() ( [echo] )");
    ExpectDump("f() { :; } >o", "f() { [:] } 1>o");
    ExpectDump("f() echo hi >o", "f() [echo hi 1>o]");
    {
        ParseResult result = MustParse("my_func() { echo; }");
        const Command& command = SingleCommand(result.commands);
        ASSERT_EQ(command.kind, CommandKind::FunctionDefinition);
        const auto& function = static_cast<const FunctionDefinition&>(command);
        EXPECT_EQ(function.name, "my_func");
        ASSERT_TRUE(function.body != nullptr);
        EXPECT_EQ(function.body->kind, CommandKind::BraceGroup);
        EXPECT_TRUE(function.redirections.empty());
    }
}

TEST(HshParserTest, ReservedWordPositions) {
    ExpectDump("echo if then fi", "[echo if then fi]");
    ExpectDump("echo } { do done case esac", "[echo } { do done case esac]");
    ExpectDump("for x in do done; do :; done", "for x in do done do [:] done");
    ExpectDump("if echo then; then echo fi; fi", "if [echo then] then [echo fi] fi");
    ExpectError("{echo a; }", "Syntax error: \"}\" unexpected", 1, false);
    ExpectError("if true then echo; fi", "Syntax error: \"fi\" unexpected (expecting \"then\")", 1, false);
}

TEST(HshParserTest, HereDocuments) {
    {
        ParseResult result = MustParse("cat <<E\nbody $x\nE\necho done\n");
        ASSERT_EQ(result.commands.items.size(), 2u);
        EXPECT_EQ(DumpCommandList(result.commands), "[cat 0<<E]; [echo done]");
        const Command& first = *result.commands.items[0].andOr.pipelines[0].commands[0];
        const auto& cat = static_cast<const SimpleCommand&>(first);
        ASSERT_EQ(cat.redirections.size(), 1u);
        EXPECT_EQ(cat.redirections[0].kind, RedirectionKind::HereDoc);
        ASSERT_TRUE(cat.redirections[0].hereDoc != nullptr);
        EXPECT_EQ(cat.redirections[0].hereDoc->rawBody, "body $x\n");
        EXPECT_TRUE(cat.redirections[0].hereDoc->terminated);
    }
    {
        // Two heredocs on one line, filled in order.
        ParseResult result = MustParse("cat <<A; cat <<B\nboA\nA\nboB\nB\n");
        EXPECT_EQ(DumpCommandList(result.commands), "[cat 0<<A]; [cat 0<<B]");
        const auto& first = static_cast<const SimpleCommand&>(
            *result.commands.items[0].andOr.pipelines[0].commands[0]);
        const auto& second = static_cast<const SimpleCommand&>(
            *result.commands.items[1].andOr.pipelines[0].commands[0]);
        ASSERT_TRUE(first.redirections[0].hereDoc != nullptr);
        ASSERT_TRUE(second.redirections[0].hereDoc != nullptr);
        EXPECT_EQ(first.redirections[0].hereDoc->rawBody, "boA\n");
        EXPECT_EQ(second.redirections[0].hereDoc->rawBody, "boB\n");
    }
    ExpectDump("cat <<-E\n\tx\n\tE\n", "[cat 0<<-E]");
    ExpectDump("c 3<<E\nb\nE\n", "[c 3<<E]");
}

TEST(HshParserTest, SyntaxErrors) {
    ExpectError("if true; fi", "Syntax error: \"fi\" unexpected (expecting \"then\")", 1, false);
    ExpectError("while true; done", "Syntax error: \"done\" unexpected (expecting \"do\")", 1, false);
    ExpectError("until false; do echo", "Syntax error: end of file unexpected (expecting \"done\")", 1, true);
    ExpectError("{ echo; ", "Syntax error: end of file unexpected (expecting \"}\")", 1, true);
    ExpectError("( echo", "Syntax error: end of file unexpected (expecting \")\")", 1, true);
    ExpectError("(\n)", "Syntax error: \")\" unexpected", 2, false);
    ExpectError("echo |\n|", "Syntax error: \"|\" unexpected", 2, false);
    ExpectError("echo ; ;", "Syntax error: \";\" unexpected", 1, false);
    ExpectError("echo ;;", "Syntax error: \";;\" unexpected", 1, false);
    ExpectError("echo a & & echo b", "Syntax error: \"&\" unexpected", 1, false);
    ExpectError("echo a &; echo", "Syntax error: \";\" unexpected", 1, false);
    ExpectError("| x", "Syntax error: \"|\" unexpected", 1, false);
    ExpectError("echo )", "Syntax error: \")\" unexpected", 1, false);
    ExpectError("echo &&", "Syntax error: end of file unexpected", 1, true);
    ExpectError("echo |", "Syntax error: end of file unexpected", 1, true);
    ExpectError("echo >", "Syntax error: end of file unexpected", 1, true);
    ExpectError("cat <<", "Syntax error: end of file unexpected", 1, true);
    ExpectError("echo > ;", "Syntax error: \";\" unexpected", 1, false);
    ExpectError("echo > > x", "Syntax error: redirection unexpected", 1, false);
    ExpectError("then", "Syntax error: \"then\" unexpected", 1, false);
    ExpectError("esac", "Syntax error: \"esac\" unexpected", 1, false);
    ExpectError("{ }", "Syntax error: \"}\" unexpected", 1, false);
    ExpectError("( )", "Syntax error: \")\" unexpected", 1, false);
    ExpectError("! ! true", "Syntax error: \"!\" unexpected", 1, false);
    ExpectError("case x foo", "Syntax error: word unexpected (expecting \"in\")", 1, false);
    ExpectError("case x in a b) ;; esac", "Syntax error: word unexpected (expecting \")\")", 1, false);
    ExpectError("case x in a) ;; b", "Syntax error: end of file unexpected (expecting \")\")", 1, true);
    ExpectError("case x in x) echo y esac", "Syntax error: end of file unexpected (expecting \";;\")", 1, true);
    ExpectError("for x in a b c ; echo; done", "Syntax error: word unexpected (expecting \"do\")", 1, false);
    ExpectError("for x y", "Syntax error: word unexpected (expecting \"do\")", 1, false);
    ExpectError("for 1 in a; do :; done", "Syntax error: Bad for loop variable", 1, false);
    ExpectError("for", "Syntax error: Bad for loop variable", 1, false);
    ExpectError("f-g() { :; }", "Syntax error: Bad function name", 1, false);
    ExpectError("\"f\"() { :; }", "Syntax error: Bad function name", 1, false);
    ExpectError("f()", "Syntax error: end of file unexpected", 1, true);
    ExpectError("f() ;", "Syntax error: \";\" unexpected", 1, false);
    ExpectError("echo 2>&a", "Syntax error: Bad fd number", 1, false);
    ExpectError("echo 1>&12", "Syntax error: Bad fd number", 1, false);
    ExpectError("echo >&1x", "Syntax error: Bad fd number", 1, false);
    ExpectError("if true; then echo; fi foo", "Syntax error: word unexpected", 1, false);
    ExpectError("(echo) foo", "Syntax error: word unexpected", 1, false);
    ExpectError("echo \"abc", "Syntax error: Unterminated quoted string", 1, true);
    // A found newline is reported on the line it ends into, as dash does.
    ExpectError("if a; then b; fi >\n", "Syntax error: newline unexpected", 2, false);
    ExpectError("case x in a\necho m;; esac", "Syntax error: newline unexpected (expecting \")\")", 2, false);
}

TEST(HshParserTest, SourceTextRoundTrips) {
    const std::string sources[] = {
        "a | b && c &",
        "x=1 echo \"a b\" 2>&1; d",
        "echo a \\\nb",
        "echo a # c",
        "{ sleep 1; echo $(echo \")\"); } >o &",
        "if a; then b; fi & c",
        "cat <<E && echo b; echo c\nx $y\nE\n",
        "cat <<A & cat <<B\nba\nA\nbb\nB\n",
        "cat <<-E\n\tx\n\tE\n",
        "while cat <<E; do\nhdbody\nE\necho b\ndone\n",
        "case x in a) cat <<E;; esac\nbody\nE\n",
        "if cat <<E; then echo; fi\nB\nE\n",
        "cat <<E | wc\nhello world\nE\n",
        "f() { echo f; } >o",
        "g()\n(echo g)",
        "{ a & b; }",
        "a && b; c || d &",
        "for x in 1 2; do echo $x; done",
        "case $v in a) echo m;; esac",
    };
    for (const std::string& source : sources) {
        ExpectSourceTextsRoundTrip(source);
    }
    // Exact sourceText values.
    {
        ParseResult result = MustParse("a | b && c &");
        ASSERT_EQ(result.commands.items.size(), 1u);
        EXPECT_EQ(result.commands.items[0].sourceText, "a | b && c");
        EXPECT_TRUE(result.commands.items[0].background);
    }
    {
        ParseResult result = MustParse("x=1 echo \"a b\" 2>&1; d");
        ASSERT_EQ(result.commands.items.size(), 2u);
        EXPECT_EQ(result.commands.items[0].sourceText, "x=1 echo \"a b\" 2>&1");
        EXPECT_EQ(result.commands.items[1].sourceText, "d");
    }
    {
        ParseResult result = MustParse("echo a \\\nb");
        EXPECT_EQ(result.commands.items[0].sourceText, "echo a \\\nb");
    }
    {
        ParseResult result = MustParse("echo a # c"); // the comment is dropped
        ASSERT_EQ(result.commands.items.size(), 1u);
        EXPECT_EQ(result.commands.items[0].sourceText, "echo a");
    }
    {
        ParseResult result = MustParse("{ sleep 1; echo $(echo \")\"); } >o &");
        ASSERT_EQ(result.commands.items.size(), 1u);
        EXPECT_EQ(result.commands.items[0].sourceText, "{ sleep 1; echo $(echo \")\"); } >o");
        const auto& group = static_cast<const BraceGroup&>(
            *result.commands.items[0].andOr.pipelines[0].commands[0]);
        ASSERT_EQ(group.body.items.size(), 2u);
        EXPECT_EQ(group.body.items[0].sourceText, "sleep 1");
        EXPECT_EQ(group.body.items[1].sourceText, "echo $(echo \")\")");
    }
    {
        ParseResult result = MustParse("if a; then b; fi & c");
        ASSERT_EQ(result.commands.items.size(), 2u);
        EXPECT_EQ(result.commands.items[0].sourceText, "if a; then b; fi");
        EXPECT_EQ(result.commands.items[1].sourceText, "c");
    }
    {
        // A heredoc body follows the line: appended after a newline.
        ParseResult result = MustParse("cat <<E && echo b; echo c\nx $y\nE\n");
        ASSERT_EQ(result.commands.items.size(), 2u);
        EXPECT_EQ(result.commands.items[0].sourceText, "cat <<E && echo b\nx $y\nE\n");
        EXPECT_EQ(result.commands.items[1].sourceText, "echo c");
    }
    {
        // Each item gets only its own heredoc's body.
        ParseResult result = MustParse("cat <<A & cat <<B\nba\nA\nbb\nB\n");
        ASSERT_EQ(result.commands.items.size(), 2u);
        EXPECT_EQ(result.commands.items[0].sourceText, "cat <<A\nba\nA\n");
        EXPECT_EQ(result.commands.items[1].sourceText, "cat <<B\nbb\nB\n");
    }
    {
        ParseResult result = MustParse("cat <<-E\n\tx\n\tE\n");
        EXPECT_EQ(result.commands.items[0].sourceText, "cat <<-E\n\tx\n\tE\n");
    }
    {
        ParseResult result = MustParse("f() { echo f; } >o");
        const auto& function = static_cast<const FunctionDefinition&>(
            *result.commands.items[0].andOr.pipelines[0].commands[0]);
        EXPECT_EQ(function.sourceText, "f() { echo f; } >o");
    }
    {
        ParseResult result = MustParse("g()\n(echo g)");
        const auto& function = static_cast<const FunctionDefinition&>(
            *result.commands.items[0].andOr.pipelines[0].commands[0]);
        EXPECT_EQ(function.sourceText, "g()\n(echo g)");
    }
    {
        // A heredoc body read after the inner list or the function has ended
        // is still appended to its text.
        ParseResult result = MustParse("{ cat <<E & }\nb\nE\n");
        const auto& group = static_cast<const BraceGroup&>(
            *result.commands.items[0].andOr.pipelines[0].commands[0]);
        ASSERT_EQ(group.body.items.size(), 1u);
        EXPECT_EQ(group.body.items[0].sourceText, "cat <<E\nb\nE\n");
        EXPECT_EQ(result.commands.items[0].sourceText, "{ cat <<E & }\nb\nE\n");
    }
    {
        ParseResult result = MustParse("case x in a) cat <<E;; esac\nbody\nE\n");
        const auto& caseCommand = static_cast<const CaseCommand&>(
            *result.commands.items[0].andOr.pipelines[0].commands[0]);
        ASSERT_EQ(caseCommand.items.size(), 1u);
        EXPECT_EQ(caseCommand.items[0].body.items[0].sourceText, "cat <<E\nbody\nE\n");
    }
    {
        ParseResult result = MustParse("f() { cat <<E; } && :\nb\nE\n");
        const auto& function = static_cast<const FunctionDefinition&>(
            *result.commands.items[0].andOr.pipelines[0].commands[0]);
        EXPECT_EQ(function.sourceText, "f() { cat <<E; }\nb\nE\n");
    }
    {
        ParseResult result = MustParse("{ a & b; }");
        const auto& group = static_cast<const BraceGroup&>(
            *result.commands.items[0].andOr.pipelines[0].commands[0]);
        ASSERT_EQ(group.body.items.size(), 2u);
        EXPECT_EQ(group.body.items[0].sourceText, "a");
        EXPECT_TRUE(group.body.items[0].background);
        EXPECT_EQ(group.body.items[1].sourceText, "b");
    }
}

TEST(HshParserTest, CommandSubstitutionChecked) {
    // An error inside $(...) is an error of the whole parse; the wording comes
    // from parsing the text alone, so it names ")" rather than the outer line.
    // Never incomplete: the substitution is closed, so more input cannot help.
    ExpectError("echo $(if)", "Syntax error: \")\" unexpected (expecting \"then\")", 1, false);
    {
        ParserOptions interactive;
        interactive.interactive = true;
        ExpectError("echo $(if)\n", "Syntax error: \")\" unexpected (expecting \"then\")", 1,
                    false, interactive);
    }
    ExpectError("echo \"$(fi)\"", "Syntax error: \"fi\" unexpected", 1, false);
    ExpectDump("echo $(echo ok)", "[echo $(echo ok)]");
    ExpectDump("x=$(case a in a) echo;; esac)", "[x=$(case a in a) echo;; esac)]");
    ExpectDump("echo $(echo $(echo deep))", "[echo $(echo $(echo deep))]");
    ExpectDump("echo `if true; then echo ok; fi`", "[echo `if true; then echo ok; fi`]");
}

TEST(HshParserTest, ParseNextOneLineAtATime) {
    {
        // A complete command is returned before a later line's error is seen.
        Parser parser("echo a\nfi\n");
        ParseResult first = parser.ParseNext();
        ASSERT_EQ(first.status, ParseResult::Status::Command);
        EXPECT_EQ(DumpCommandList(first.commands), "[echo a]");
        ParseResult second = parser.ParseNext();
        ASSERT_EQ(second.status, ParseResult::Status::Error);
        EXPECT_EQ(second.errorMessage, "Syntax error: \"fi\" unexpected");
        EXPECT_EQ(second.errorLine, 2);
        // After an error, every later call returns it again.
        ParseResult third = parser.ParseNext();
        ASSERT_EQ(third.status, ParseResult::Status::Error);
        EXPECT_EQ(third.errorMessage, second.errorMessage);
        EXPECT_EQ(third.errorLine, 2);
    }
    {
        Parser parser("\n\necho b\n");
        ParseResult result = parser.ParseNext();  // empty lines are skipped
        ASSERT_EQ(result.status, ParseResult::Status::Command);
        EXPECT_EQ(DumpCommandList(result.commands), "[echo b]");
        EXPECT_EQ(parser.ParseNext().status, ParseResult::Status::EndOfInput);
    }
    {
        Parser parser("if true\nthen echo t\nfi\necho c\n");
        ParseResult first = parser.ParseNext();
        ASSERT_EQ(first.status, ParseResult::Status::Command);
        EXPECT_EQ(DumpCommandList(first.commands), "if [true] then [echo t] fi");
        ParseResult second = parser.ParseNext();
        ASSERT_EQ(second.status, ParseResult::Status::Command);
        EXPECT_EQ(DumpCommandList(second.commands), "[echo c]");
        EXPECT_EQ(parser.ParseNext().status, ParseResult::Status::EndOfInput);
    }
    {
        // One line may hold several items; it is one complete command.
        Parser parser("echo a; echo b; echo c\n");
        ParseResult result = parser.ParseNext();
        ASSERT_EQ(result.status, ParseResult::Status::Command);
        EXPECT_EQ(DumpCommandList(result.commands), "[echo a]; [echo b]; [echo c]");
        EXPECT_EQ(parser.ParseNext().status, ParseResult::Status::EndOfInput);
    }
    {
        // An empty source is EndOfInput at once.
        Parser parser("");
        EXPECT_EQ(parser.ParseNext().status, ParseResult::Status::EndOfInput);
        Parser commentsOnly("# just a comment\n");
        EXPECT_EQ(commentsOnly.ParseNext().status, ParseResult::Status::EndOfInput);
        ParseResult empty = ParseProgram("");
        ASSERT_EQ(empty.status, ParseResult::Status::Command);
        EXPECT_EQ(DumpCommandList(empty.commands), "");
    }
}

TEST(HshParserTest, LinesAndFirstLine) {
    {
        Parser parser("echo a\necho b | cat\nif c\nthen d\nfi\n");
        ParseResult r1 = parser.ParseNext();
        ASSERT_EQ(r1.status, ParseResult::Status::Command);
        EXPECT_EQ(r1.commands.items[0].andOr.pipelines[0].commands[0]->line, 1);
        ParseResult r2 = parser.ParseNext();
        ASSERT_EQ(r2.status, ParseResult::Status::Command);
        EXPECT_EQ(r2.commands.items[0].andOr.pipelines[0].commands[0]->line, 2);
        EXPECT_EQ(r2.commands.items[0].andOr.pipelines[0].commands[1]->line, 2);
        ParseResult r3 = parser.ParseNext();
        ASSERT_EQ(r3.status, ParseResult::Status::Command);
        EXPECT_EQ(r3.commands.items[0].andOr.pipelines[0].commands[0]->line, 3);
    }
    {
        ParserOptions options;
        options.firstLine = 10;
        ParseResult result = ParseProgram("echo a\nfi\n", options);
        // ParseProgram stops at the first error; parse one command at a time.
        ASSERT_EQ(result.status, ParseResult::Status::Error);
        EXPECT_EQ(result.errorLine, 11);
        Parser parser("echo a\n", options);
        ParseResult first = parser.ParseNext();
        ASSERT_EQ(first.status, ParseResult::Status::Command);
        EXPECT_EQ(first.commands.items[0].andOr.pipelines[0].commands[0]->line, 10);
        Parser error("fi\n", options);
        ParseResult err = error.ParseNext();
        ASSERT_EQ(err.status, ParseResult::Status::Error);
        EXPECT_EQ(err.errorLine, 10);
        EXPECT_EQ(err.errorMessage, "Syntax error: \"fi\" unexpected");
    }
}

TEST(HshParserTest, InteractiveIncomplete) {
    ParserOptions interactive;
    interactive.interactive = true;
    ExpectError("cat <<E\nx\n", "Syntax error: end of file unexpected", 3, true, interactive);
    ExpectError("echo a \\\n", "Syntax error: end of file unexpected", 2, true, interactive);
    ExpectError("if true\n", "Syntax error: end of file unexpected (expecting \"then\")", 2, true, interactive);
    ExpectError("echo \"a\n", "Syntax error: Unterminated quoted string", 2, true, interactive);
    {
        Parser parser("echo a\n", interactive);
        ParseResult result = parser.ParseNext();
        ASSERT_EQ(result.status, ParseResult::Status::Command);
        EXPECT_EQ(DumpCommandList(result.commands), "[echo a]");
    }
    ExpectError("fi\n", "Syntax error: \"fi\" unexpected", 1, false, interactive);
    // The same sources are complete or plain errors without `interactive`.
    ParseResult result = ParseProgram("cat <<E\nx\n");
    ASSERT_EQ(result.status, ParseResult::Status::Command);
    EXPECT_EQ(DumpCommandList(result.commands), "[cat 0<<E]");
    ExpectDump("echo a \\\n", "[echo a]");
}
