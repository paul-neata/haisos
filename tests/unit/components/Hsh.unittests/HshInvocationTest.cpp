#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "commands/hsh/HshInvocation.h"

namespace Haisos::Hsh {
namespace {

// One ParseInvocation case: what it was given and what it must come out as.
struct InvocationCase {
    std::vector<std::string> args;
    Invocation::Source source = Invocation::Source::StandardInput;
    std::string commandString;
    std::string scriptPath;
    std::string arg0 = "hsh";
    std::vector<std::string> positional;
    std::string letters;  // $-: OptionLetters of the options
    std::vector<std::string> notTreated;
    std::string error;
};

std::string Describe(const std::vector<std::string>& args) {
    std::string text;
    for (const std::string& arg : args) {
        text += (text.empty() ? "" : " ") + arg;
    }
    return text.empty() ? std::string("<none>") : text;
}

void ExpectInvocation(const InvocationCase& expected) {
    const Invocation invocation = ParseInvocation(expected.args);
    const std::string described = Describe(expected.args);
    EXPECT_EQ(invocation.source, expected.source) << described;
    EXPECT_EQ(invocation.commandString, expected.commandString) << described;
    EXPECT_EQ(invocation.scriptPath, expected.scriptPath) << described;
    EXPECT_EQ(invocation.arg0, expected.arg0) << described;
    EXPECT_EQ(invocation.positional, expected.positional) << described;
    EXPECT_EQ(OptionLetters(invocation.options), expected.letters) << described;
    EXPECT_EQ(invocation.notTreated, expected.notTreated) << described;
    EXPECT_EQ(invocation.error, expected.error) << described;
}

} // namespace

TEST(HshInvocationTest, Sources) {
    ExpectInvocation({{"-c", "x"}, Invocation::Source::CommandString, "x"});
    ExpectInvocation({{"-c", "x", "nm", "a", "b"}, Invocation::Source::CommandString, "x", "", "nm", {"a", "b"}});
    ExpectInvocation({{"s.sh", "a"}, Invocation::Source::ScriptFile, "", "s.sh", "s.sh", {"a"}});
    ExpectInvocation({{}, Invocation::Source::StandardInput, "", "", "hsh", {}, "s"});
    ExpectInvocation({{"-s", "a"}, Invocation::Source::StandardInput, "", "", "hsh", {"a"}, "s"});
    ExpectInvocation({{"--", "s.sh"}, Invocation::Source::ScriptFile, "", "s.sh", "s.sh"});
    // A lone "-" ends the options; what follows are operands.
    ExpectInvocation({{"-", "-c", "x"}, Invocation::Source::ScriptFile, "", "-c", "-c", {"x"}});
    // After the -c string the words are $0 and $1...; "--" is not special there.
    ExpectInvocation({{"-c", "x", "--", "a"}, Invocation::Source::CommandString, "x", "", "--", {"a"}});
    ExpectInvocation({{"-ec", "x"}, Invocation::Source::CommandString, "x", "", "hsh", {}, "e"});
}

TEST(HshInvocationTest, Options) {
    ExpectInvocation({{"-eu", "-c", "x"}, Invocation::Source::CommandString, "x", "", "hsh", {}, "ue"});
    ExpectInvocation({{"-o", "errexit", "-c", "x"}, Invocation::Source::CommandString, "x", "", "hsh", {}, "e"});
    ExpectInvocation({{"+o", "errexit", "-e", "-c", "x"}, Invocation::Source::CommandString, "x", "", "hsh", {}, "e"});
    ExpectInvocation({{"-eu", "+e", "-c", "x"}, Invocation::Source::CommandString, "x", "", "hsh", {}, "u"});
    ExpectInvocation({{"-aCfnx", "-c", "x"}, Invocation::Source::CommandString, "x", "", "hsh", {}, "aCxnf"});
    ExpectInvocation({{"-m", "+V", "-o", "monitor", "-l", "-c", "x"},
        Invocation::Source::CommandString, "x", "", "hsh", {}, "", {"-m", "+V", "-o monitor", "-l"}});
}

TEST(HshInvocationTest, Errors) {
    ExpectInvocation({{"-y"}, Invocation::Source::StandardInput, "", "", "hsh", {}, "", {}, "Illegal option -y"});
    ExpectInvocation({{"+y"}, Invocation::Source::StandardInput, "", "", "hsh", {}, "", {}, "Illegal option +y"});
    ExpectInvocation({{"-c"}, Invocation::Source::StandardInput, "", "", "hsh", {}, "", {}, "-c requires an argument"});
    ExpectInvocation({{"-o", "nosuch"}, Invocation::Source::StandardInput, "", "", "hsh", {}, "", {}, "Illegal option -o nosuch"});
    ExpectInvocation({{"-o"}, Invocation::Source::StandardInput, "", "", "hsh", {}, "", {}, "-o requires an argument"});
    ExpectInvocation({{"--frob"}, Invocation::Source::StandardInput, "", "", "hsh", {}, "", {}, "Illegal option --"});
}

TEST(HshInvocationTest, OptionTable) {
    // dash's options, in the order `set -o` lists them.
    const std::vector<std::string> names = {
        "errexit", "noglob", "ignoreeof", "interactive", "monitor", "noexec",
        "stdin", "xtrace", "verbose", "vi", "emacs", "noclobber", "allexport",
        "notify", "nounset", "privileged", "nolog", "debug"};
    const std::string letters = "efIimnsxvVECabup";
    const auto& table = ShellOptionTable();
    ASSERT_EQ(table.size(), names.size());
    for (size_t i = 0; i < table.size(); ++i) {
        EXPECT_EQ(table[i].name, names[i]) << i;
        EXPECT_EQ(table[i].letter, i < letters.size() ? letters[i] : '\0') << names[i];
    }
    // Treated: e f i n s x C a u; the rest are reported as not treated.
    for (const ShellOptionInfo& info : table) {
        const bool treated = std::string("efinsxCau").find(info.letter) != std::string::npos;
        EXPECT_EQ(info.field != nullptr, treated) << info.name;
    }
    EXPECT_EQ(FindShellOption('e')->name, std::string("errexit"));
    EXPECT_EQ(FindShellOption("nolog")->letter, 0);
    EXPECT_EQ(FindShellOption("nolog")->field, nullptr);
    EXPECT_EQ(FindShellOption('q'), nullptr);
    EXPECT_EQ(FindShellOption("nosuch"), nullptr);
}

} // namespace Haisos::Hsh
