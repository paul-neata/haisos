#include <gtest/gtest.h>

#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "commands/hsh/HshAst.h"
#include "commands/hsh/HshError.h"
#include "commands/hsh/HshExpansion.h"
#include "commands/hsh/HshParser.h"

using namespace Haisos::Hsh;
using Haisos::DirectoryEntry;
using Haisos::DirectoryEntryType;

namespace {

DirectoryEntry File_(std::string name) { return DirectoryEntry{std::move(name), DirectoryEntryType::File}; }
DirectoryEntry Dir_(std::string name) { return DirectoryEntry{std::move(name), DirectoryEntryType::Dir}; }

// A fake host: directory listings for globbing, canned outputs and statuses
// for command substitutions, and a record of what it was asked to run.
class FakeExpansionHost : public IExpansionHost {
public:
    std::map<std::string, std::vector<DirectoryEntry>> dirs;
    std::map<std::string, CommandSubstitutionResult> commands;
    std::vector<std::pair<std::string, int>> runs;  // (source, line)

    std::vector<DirectoryEntry> ReadDirectory(const std::string& path) override {
        auto it = dirs.find(path);
        return it == dirs.end() ? std::vector<DirectoryEntry>{} : it->second;
    }

    bool Exists(const std::string& path) override {
        std::string p = path;
        while (p.size() > 1 && p.back() == '/') p.pop_back();
        size_t slash = p.find_last_of('/');
        std::string parent, name;
        if (slash == std::string::npos) { parent = "."; name = p; }
        else if (slash == 0) { parent = "/"; name = p.substr(1); }
        else { parent = p.substr(0, slash); name = p.substr(slash + 1); }
        auto it = dirs.find(parent);
        if (it == dirs.end()) return false;
        for (const DirectoryEntry& entry : it->second)
            if (entry.name == name) return true;
        return false;
    }

    CommandSubstitutionResult RunCommandSubstitution(const std::string& source, int line) override {
        runs.emplace_back(source, line);
        auto it = commands.find(source);
        return it == commands.end() ? CommandSubstitutionResult{} : it->second;
    }
};

// A shell state and a fake host, plus helpers that expand shell text: the
// words to expand are written as shell source and parsed with ParseProgram.
struct Fixture {
    ShellState state;
    FakeExpansionHost host;

    CommandPtr FirstCommand(const std::string& source) {
        ParseResult result = ParseProgram(source);
        if (result.status != ParseResult::Status::Command || result.commands.items.empty()) {
            throw std::runtime_error("test source did not parse: " + source);
        }
        const AndOrList& andOr = result.commands.items[0].andOr;
        return andOr.pipelines.at(0).commands.at(0);
    }

    static const SimpleCommand& Simple(const Command& command) {
        if (command.kind != CommandKind::Simple) {
            throw std::runtime_error("test source is no simple command");
        }
        return static_cast<const SimpleCommand&>(command);
    }

    // Every word of the first simple command, expanded.
    std::vector<std::string> ExpandWordsIn(const std::string& source) {
        CommandPtr command = FirstCommand(source);
        const SimpleCommand& simple = Simple(*command);
        return Expander(state, host).ExpandWords(simple.words);
    }

    // "echo <wordSource>", the echo dropped: the fields of the given words.
    std::vector<std::string> Expand(const std::string& wordSource) {
        std::vector<std::string> fields = ExpandWordsIn("echo " + wordSource);
        if (fields.empty() || fields[0] != "echo") {
            throw std::runtime_error("test expander lost the echo: " + wordSource);
        }
        fields.erase(fields.begin());
        return fields;
    }

    // The first assignment's value, expanded ("x=...").
    std::string AssignmentValue(const std::string& source) {
        CommandPtr command = FirstCommand(source);
        const SimpleCommand& simple = Simple(*command);
        if (simple.assignments.empty()) {
            throw std::runtime_error("test source has no assignment: " + source);
        }
        return Expander(state, host).ExpandAssignmentValue(simple.assignments[0].value);
    }

    // The first redirection's target, expanded as a string ("echo hi > ...").
    std::string RedirectTarget(const std::string& source) {
        CommandPtr command = FirstCommand(source);
        const SimpleCommand& simple = Simple(*command);
        if (simple.redirections.empty()) {
            throw std::runtime_error("test source has no redirection: " + source);
        }
        return Expander(state, host).ExpandToString(simple.redirections[0].target);
    }

    // The case subject, expanded as a string ("case <subject> in x) ;; esac").
    std::string CaseSubject(const std::string& source) {
        CommandPtr command = FirstCommand(source);
        if (command->kind != CommandKind::Case) {
            throw std::runtime_error("test source is no case: " + source);
        }
        return Expander(state, host).ExpandToString(static_cast<const CaseCommand&>(*command).subject);
    }

    // The first case item's patterns, each expanded as a pattern.
    std::vector<std::string> CasePatterns(const std::string& source) {
        CommandPtr command = FirstCommand(source);
        if (command->kind != CommandKind::Case) {
            throw std::runtime_error("test source is no case: " + source);
        }
        const CaseCommand& caseCommand = static_cast<const CaseCommand&>(*command);
        std::vector<std::string> patterns;
        for (const Word& pattern : caseCommand.items.at(0).patterns) {
            patterns.push_back(Expander(state, host).ExpandPattern(pattern));
        }
        return patterns;
    }

    // The for-loop's words, expanded ("for v in ...; do :; done").
    std::vector<std::string> ForWords(const std::string& source) {
        CommandPtr command = FirstCommand(source);
        if (command->kind != CommandKind::For) {
            throw std::runtime_error("test source is no for: " + source);
        }
        return Expander(state, host).ExpandWords(static_cast<const ForCommand&>(*command).words);
    }

    // The heredoc body of the first redirection, expanded.
    std::string HereDocBody(const std::string& source) {
        CommandPtr command = FirstCommand(source);
        const SimpleCommand& simple = Simple(*command);
        if (simple.redirections.empty() || !simple.redirections[0].hereDoc) {
            throw std::runtime_error("test source has no heredoc: " + source);
        }
        return Expander(state, host).ExpandHereDocument(*simple.redirections[0].hereDoc);
    }

    void ExpectError(const std::string& wordSource, const std::string& message) {
        try {
            Expand(wordSource);
            FAIL() << "expected an error for: " << wordSource;
        } catch (const ShellError& e) {
            EXPECT_EQ(std::string(e.what()), message) << "source: " << wordSource;
        }
    }
};

using Fields = std::vector<std::string>;

TEST(HshExpansionTest, QuotingAndQuoteRemoval) {
    Fixture f;
    EXPECT_EQ(f.Expand("'a b'"), Fields({"a b"}));
    EXPECT_EQ(f.Expand("\"a b\""), Fields({"a b"}));
    EXPECT_EQ(f.Expand("a\\ b"), Fields({"a b"}));
    EXPECT_EQ(f.Expand("\"\\$x\""), Fields({"$x"}));
    EXPECT_EQ(f.Expand("\"\""), Fields({""}));
    EXPECT_EQ(f.Expand("''\"\""), Fields({""}));
    EXPECT_EQ(f.Expand("a\"\""), Fields({"a"}));
}

TEST(HshExpansionTest, Tilde) {
    Fixture f;
    f.state.variables.Set("HOME", "/h");
    EXPECT_EQ(f.Expand("~"), Fields({"/h"}));
    EXPECT_EQ(f.Expand("~/x"), Fields({"/h/x"}));
    EXPECT_EQ(f.Expand("a~"), Fields({"a~"}));
    EXPECT_EQ(f.Expand("\"~\""), Fields({"~"}));
    EXPECT_EQ(f.Expand("\\~"), Fields({"~"}));
    EXPECT_EQ(f.Expand("~\"/a\""), Fields({"~/a"}));
    EXPECT_EQ(f.Expand("~:"), Fields({"~:"}));
    EXPECT_EQ(f.Expand("~root"), Fields({"~root"})); // ~user is not supported (dash: /root)

    f.state.variables.Set("HOME", "");
    EXPECT_EQ(f.Expand("~"), Fields({})); // an empty HOME vanishes, as in dash
    EXPECT_EQ(f.Expand("~/"), Fields({"/"}));

    Fixture noHome;
    EXPECT_EQ(noHome.Expand("~/a"), Fields({"~/a"})); // HOME unset: left as it is

    Fixture spaced;
    spaced.state.variables.Set("HOME", "/a b");
    EXPECT_EQ(spaced.Expand("~"), Fields({"/a b"})); // one field

    Fixture assign;
    assign.state.variables.Set("HOME", "/h");
    EXPECT_EQ(assign.AssignmentValue("x=~:~/b:a~"), "/h:/h/b:a~");

    EXPECT_EQ(assign.Expand("${x:-~}"), Fields({"/h"}));
    EXPECT_EQ(assign.Expand("\"${x:-~}\""), Fields({"~"}));
}

TEST(HshExpansionTest, SpecialParameters) {
    Fixture f;
    f.state.positional = {"a", "b"};
    f.state.lastExitStatus = 3;
    f.state.shellPid = 4242;
    f.state.options.errexit = true;
    f.state.options.nounset = true;
    f.state.arg0 = "mysh";

    EXPECT_EQ(f.Expand("$#"), Fields({"2"}));
    EXPECT_EQ(f.Expand("$?"), Fields({"3"}));
    EXPECT_EQ(f.Expand("$$"), Fields({"4242"}));
    EXPECT_EQ(f.Expand("$-"), Fields({"ue"}));
    EXPECT_EQ(f.Expand("$0"), Fields({"mysh"}));
    EXPECT_EQ(f.Expand("$1"), Fields({"a"}));
    EXPECT_EQ(f.Expand("$10"), Fields({"a0"})); // $1, then a literal 0

    f.state.positional = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10"};
    EXPECT_EQ(f.Expand("${10}"), Fields({"10"}));

    Fixture g;
    g.state.options.nounset = true;
    g.state.lastBackgroundPid = 777;
    EXPECT_EQ(g.Expand("$!"), Fields({"777"}));

    Fixture h; // no background command ran: $! is unset (empty), or an error under -u
    EXPECT_EQ(h.Expand("$!"), Fields({}));
    h.state.options.nounset = true;
    h.ExpectError("$!", "!: parameter not set");
}

TEST(HshExpansionTest, ParameterOps) {
    {
        Fixture f;
        f.state.variables.Set("x", "1");
        EXPECT_EQ(f.Expand("${x+set}"), Fields({"set"}));
        EXPECT_EQ(f.Expand("${y+set}"), Fields({}));
        EXPECT_EQ(f.Expand("${y-unset}"), Fields({"unset"}));
        EXPECT_EQ(f.Expand("${y:-a b}"), Fields({"a", "b"})); // unset operand literals split
        EXPECT_EQ(f.Expand("\"${y:-a b}\""), Fields({"a b"}));
        EXPECT_EQ(f.Expand("${x-${y-z}}"), Fields({"1"}));
        EXPECT_EQ(f.Expand("${y-${x-z}}"), Fields({"1"}));
        EXPECT_EQ(f.Expand("${x:-${y:-z}}"), Fields({"1"}));
        EXPECT_EQ(f.Expand("${y:-${x:-z}}"), Fields({"1"}));
    }
    {
        Fixture f;
        EXPECT_EQ(f.Expand("${x-${y-z}}"), Fields({"z"}));
        EXPECT_EQ(f.Expand("${x:=a b}"), Fields({"a", "b"}));
        EXPECT_EQ(f.state.variables.Get("x"), "a b");
    }
    {
        Fixture f;
        f.state.variables.Set("x", "hello");
        EXPECT_EQ(f.Expand("${x%l*}"), Fields({"hel"}));
        EXPECT_EQ(f.Expand("${x%%l*}"), Fields({"he"}));
        EXPECT_EQ(f.Expand("${x#*l}"), Fields({"lo"}));
        EXPECT_EQ(f.Expand("${x##*l}"), Fields({"o"}));
        EXPECT_EQ(f.Expand("${#x}"), Fields({"5"}));
    }
    {
        Fixture f;
        f.state.variables.Set("x", "abc");
        EXPECT_EQ(f.Expand("${x#\"a\"}"), Fields({"bc"})); // a quoted pattern is literal
        EXPECT_EQ(f.Expand("${x#'a'}"), Fields({"bc"}));
        EXPECT_EQ(f.Expand("\"${x#'a'}\""), Fields({"bc"})); // quoted even inside "..."
        EXPECT_EQ(f.Expand("${x#\\a}"), Fields({"bc"}));
        EXPECT_EQ(f.Expand("${x#[ab]}"), Fields({"bc"}));
        EXPECT_EQ(f.Expand("${x%?}"), Fields({"ab"}));
    }
    {
        Fixture f;
        f.state.variables.Set("x", "*");
        EXPECT_EQ(f.Expand("${x#\"*\"}"), Fields({})); // "*" matches the whole value
        EXPECT_EQ(f.Expand("${x#*}"), Fields({"*"}));   // empty prefix removed: keeps x
        EXPECT_EQ(f.Expand("${x#}"), Fields({"*"}));
    }
    {
        Fixture f;
        f.state.variables.Set("x", "\xc3\xa9"); // é, two bytes
        EXPECT_EQ(f.Expand("${#x}"), Fields({"2"})); // bytes, as dash
    }
    {
        Fixture f;
        f.state.positional = {"a", "b", "c"};
        EXPECT_EQ(f.Expand("${#@}"), Fields({"5"})); // "a b c" joined, in bytes
        EXPECT_EQ(f.Expand("${#*}"), Fields({"5"}));
    }
    {
        Fixture f;
        f.state.positional = {"a", "b"};
        EXPECT_EQ(f.Expand("${@#a}"), Fields({"b"})); // applied to each parameter
    }
    {
        Fixture f; // no positional parameters
        EXPECT_EQ(f.Expand("${@-x}"), Fields({}));
        EXPECT_EQ(f.Expand("${*:-y}"), Fields({"y"}));
    }
}

TEST(HshExpansionTest, ParameterErrors) {
    {
        Fixture f;
        f.ExpectError("${x?}", "x: parameter not set");
        f.state.variables.Set("x", "");
        f.ExpectError("${x:?}", "x: parameter not set or null");
        f.state.variables.Unset("x");
        f.ExpectError("${x?a b  c}", "x: a b  c");
        f.ExpectError("${1=a}", "1: bad variable name");
    }
    {
        Fixture f;
        f.state.variables.Set("r", "");
        f.state.variables.MakeReadonly("r");
        f.ExpectError("${r:=v}", "r: is read only");
    }
    {
        Fixture f;
        f.ExpectError("${x;}", "Bad substitution");
    }
    {
        Fixture f;
        f.state.options.nounset = true;
        f.ExpectError("$x", "x: parameter not set");
        f.ExpectError("${#x}", "x: parameter not set");
        f.ExpectError("${x#a}", "x: parameter not set");
        f.ExpectError("$1", "1: parameter not set");
        EXPECT_EQ(f.Expand("${x-ok}"), Fields({"ok"}));
        EXPECT_EQ(f.Expand("$#"), Fields({"0"}));
        EXPECT_EQ(f.Expand("$@"), Fields({}));
    }
}

TEST(HshExpansionTest, PositionalParameters) {
    Fixture f;
    f.state.positional = {"a", "b c", "d"};
    EXPECT_EQ(f.Expand("\"$@\""), Fields({"a", "b c", "d"}));
    EXPECT_EQ(f.Expand("$@"), Fields({"a", "b", "c", "d"}));
    EXPECT_EQ(f.Expand("\"$*\""), Fields({"a b c d"}));

    f.state.variables.Set("IFS", ":");
    EXPECT_EQ(f.Expand("\"$*\""), Fields({"a:b c:d"}));

    f.state.variables.Set("IFS", "");
    EXPECT_EQ(f.Expand("\"$*\""), Fields({"ab cd"}));
    EXPECT_EQ(f.Expand("$*"), Fields({"a", "b c", "d"})); // empty IFS: no splitting

    f.state.variables.Unset("IFS");
    f.state.positional = {"a", "b"};
    EXPECT_EQ(f.Expand("\"x$@y\""), Fields({"xa", "by"}));
    EXPECT_EQ(f.AssignmentValue("x=\"$@\""), "a b");
    f.state.variables.Set("IFS", "x");
    EXPECT_EQ(f.AssignmentValue("x=\"$@\""), "axb");

    {
        Fixture empty; // no positional parameters
        EXPECT_EQ(empty.Expand("\"x$@y\""), Fields({"xy"}));
        EXPECT_EQ(empty.Expand("\"$@\""), Fields({}));
        EXPECT_EQ(empty.Expand("\"$@\"\"\""), Fields({""}));
    }
    {
        Fixture g;
        g.state.positional = {"", ""};
        EXPECT_EQ(g.Expand("\"$@\""), Fields({"", ""}));
        EXPECT_EQ(g.Expand("$@"), Fields({}));
    }
    {
        Fixture h;
        h.state.positional = {"", "a"};
        EXPECT_EQ(h.Expand("$@"), Fields({"a"}));
    }
}

TEST(HshExpansionTest, FieldSplitting) {
    {
        Fixture f;
        f.state.variables.Set("x", "  a  b  ");
        EXPECT_EQ(f.Expand("$x"), Fields({"a", "b"})); // the default IFS
    }
    {
        Fixture f;
        f.state.variables.Set("IFS", ":");
        f.state.variables.Set("x", ":a::b:");
        EXPECT_EQ(f.Expand("$x"), Fields({"", "a", "", "b"}));
        f.state.variables.Set("x", "");
        EXPECT_EQ(f.Expand("${x:-a:b}"), Fields({"a", "b"}));
    }
    {
        Fixture f;
        f.state.variables.Set("IFS", ": ");
        f.state.variables.Set("x", " :a : :b: ");
        EXPECT_EQ(f.Expand("$x"), Fields({"", "a", "", "b"}));
    }
    {
        Fixture f;
        f.state.variables.Set("IFS", "");
        f.state.variables.Set("x", "a  b");
        EXPECT_EQ(f.Expand("$x"), Fields({"a  b"})); // an empty IFS never splits
    }
    {
        Fixture f;
        f.state.variables.Set("x", "a b");
        EXPECT_EQ(f.Expand("\"$x\""), Fields({"a b"})); // quoted: never split
        f.state.variables.Set("x", "");
        EXPECT_EQ(f.Expand("$x \"\" $x"), Fields({""}));
    }
    {
        Fixture f;
        f.state.variables.Set("x", " ");
        EXPECT_EQ(f.Expand("$x"), Fields({}));
    }
}

TEST(HshExpansionTest, CommandSubstitution) {
    Fixture f;
    f.host.commands["echo a"] = CommandSubstitutionResult{"a\n\n", 0};
    EXPECT_EQ(f.Expand("$(echo a)"), Fields({"a"})); // trailing newlines removed
    ASSERT_EQ(f.host.runs.size(), 1u);
    EXPECT_EQ(f.host.runs[0].first, "echo a"); // the text as written
    EXPECT_EQ(f.host.runs[0].second, 1);

    f.host.commands["echo s"] = CommandSubstitutionResult{"a b\n", 5};
    EXPECT_EQ(f.Expand("$(echo s)"), Fields({"a", "b"}));  // unquoted output splits
    EXPECT_EQ(f.state.lastExitStatus, 5);
    EXPECT_EQ(f.Expand("\"$(echo s)\""), Fields({"a b"})); // quoted output does not

    f.host.commands["false"] = CommandSubstitutionResult{"", 1};
    EXPECT_EQ(f.Expand("$(false)"), Fields({}));
    EXPECT_EQ(f.state.lastExitStatus, 1);
    f.state.lastExitStatus = 0;
    EXPECT_EQ(f.AssignmentValue("x=$(false)"), "");
    EXPECT_EQ(f.state.lastExitStatus, 1); // x=$(false) leaves $? at 1

    f.host.commands["echo d"] = CommandSubstitutionResult{"d\n", 7};
    EXPECT_EQ(f.Expand("${x:-$(echo d)}"), Fields({"d"})); // nested in an operand
    EXPECT_EQ(f.state.lastExitStatus, 7);

    f.host.commands["echo q"] = CommandSubstitutionResult{"q\n", 0};
    EXPECT_EQ(f.Expand("`echo q`"), Fields({"q"}));
    EXPECT_EQ(f.host.runs.back().first, "echo q"); // after backquote processing

    // The line handed to the host is the line the substitution's text starts on.
    Fixture lines;
    lines.host.commands["echo b"] = CommandSubstitutionResult{"b\n", 0};
    ParseResult parsed = ParseProgram("echo x\necho $(echo b)");
    ASSERT_EQ(parsed.status, ParseResult::Status::Command);
    ASSERT_GE(parsed.commands.items.size(), 2u);
    const SimpleCommand& second =
        Fixture::Simple(*parsed.commands.items[1].andOr.pipelines.at(0).commands.at(0));
    EXPECT_EQ(Expander(lines.state, lines.host).ExpandWords(second.words), Fields({"echo", "b"}));
    ASSERT_EQ(lines.host.runs.size(), 1u);
    EXPECT_EQ(lines.host.runs[0].second, 2);
}

TEST(HshExpansionTest, Arithmetic) {
    Fixture f;
    EXPECT_EQ(f.Expand("$((1+2))"), Fields({"3"}));
    f.state.variables.Set("x", "2");
    EXPECT_EQ(f.Expand("$(($x * 3))"), Fields({"6"}));
    EXPECT_EQ(f.Expand("$((x=3))"), Fields({"3"}));
    EXPECT_EQ(f.state.variables.Get("x"), "3");
    EXPECT_EQ(f.Expand("$((y))"), Fields({"0"})); // an unset variable reads as 0
    EXPECT_EQ(f.Expand("\"$((1+2))\""), Fields({"3"}));

    f.host.commands["echo 2"] = CommandSubstitutionResult{"2\n", 0};
    EXPECT_EQ(f.Expand("$((1 + $(echo 2)))"), Fields({"3"}));

    f.ExpectError("$((1/0))", "arithmetic expression: division by zero: \"1/0\"");

    f.state.variables.Set("r", "1");
    f.state.variables.MakeReadonly("r");
    f.ExpectError("$((r=5))", "r: is read only");
    EXPECT_EQ(f.state.variables.Get("r"), "1");
}

TEST(HshExpansionTest, PathnameExpansion) {
    auto makeFixture = [] {
        Fixture f;
        f.host.dirs["."] = {
            Dir_("."), Dir_(".."),
            File_("B"), File_("C"), File_("a"), File_("ab"), File_("a b"),
            File_(".hid"), Dir_("d1"), Dir_("d2"),
        };
        return f;
    };
    {
        Fixture f = makeFixture();
        EXPECT_EQ(f.Expand("*"), Fields({"B", "C", "a", "a b", "ab", "d1", "d2"}));
        EXPECT_EQ(f.Expand("a*"), Fields({"a", "a b", "ab"}));
        EXPECT_EQ(f.Expand("\"a\"*"), Fields({"a", "a b", "ab"}));
        EXPECT_EQ(f.Expand("a\\*"), Fields({"a*"})); // a quoted * is literal
        EXPECT_EQ(f.Expand("\"*\""), Fields({"*"}));
        f.state.variables.Set("x", "a*");
        EXPECT_EQ(f.Expand("$x"), Fields({"a", "a b", "ab"})); // an expanded * globs
        EXPECT_EQ(f.Expand("\"$x\""), Fields({"a*"}));
        f.state.variables.Set("x", "[a]");
        EXPECT_EQ(f.Expand("$x"), Fields({"a"}));
        EXPECT_EQ(f.Expand("nomatch*"), Fields({"nomatch*"})); // kept when nothing matches
    }
    {
        Fixture f = makeFixture();
        f.state.options.noglob = true;
        EXPECT_EQ(f.Expand("*"), Fields({"*"}));
    }
}

TEST(HshExpansionTest, Patterns) {
    Fixture f;
    EXPECT_EQ(f.CasePatterns("case s in \"a*\") ;; esac"), Fields({"a\\*"}));
    EXPECT_EQ(f.CasePatterns("case s in a*) ;; esac"), Fields({"a*"}));
    f.state.variables.Set("x", "*");
    EXPECT_EQ(f.CasePatterns("case s in $x) ;; esac"), Fields({"*"}));   // an expanded * keeps its meaning
    EXPECT_EQ(f.CasePatterns("case s in \"$x\") ;; esac"), Fields({"\\*"}));
    EXPECT_EQ(f.CasePatterns("case s in \\\\) ;; esac"), Fields({"\\\\"}));
}

TEST(HshExpansionTest, StringContexts) {
    Fixture f;
    f.state.variables.Set("x", "a  b");
    EXPECT_EQ(f.RedirectTarget("echo hi > $x"), "a  b"); // no splitting in a redirection
    EXPECT_EQ(f.RedirectTarget("echo hi > *"), "*");     // no globbing either
    EXPECT_EQ(f.CaseSubject("case $x in x) ;; esac"), "a  b");

    // A quoted heredoc body is taken as it is.
    EXPECT_EQ(f.HereDocBody("cat <<'EOF'\n$x ~ end\nEOF\n"), "$x ~ end\n");

    // An unquoted heredoc body: parameters, command substitutions and
    // arithmetic expand; a tilde and quotes are kept.
    f.host.commands["echo h"] = CommandSubstitutionResult{"hi\n", 0};
    EXPECT_EQ(f.HereDocBody("cat <<EOF\n$x $(echo h) $((1+1)) ~ \"\nEOF\n"),
              "a  b hi 2 ~ \"\n");
}

TEST(HshExpansionTest, ForLoopAndAssignments) {
    Fixture f;
    f.state.variables.Set("x", "a b");
    EXPECT_EQ(f.ForWords("for v in $x 'q r'; do :; done"), Fields({"a", "b", "q r"}));

    // An assignment's value: no splitting, no globbing.
    EXPECT_EQ(f.AssignmentValue("v=$x"), "a b");
    EXPECT_EQ(f.AssignmentValue("v=*"), "*");
}

} // namespace
