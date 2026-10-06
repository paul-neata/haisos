#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Environment.h"
#include "commands/hsh/HshVariables.h"

using namespace Haisos::Hsh;

TEST(HshVariablesTest, SetGetUnset) {
    ShellVariables variables;
    EXPECT_FALSE(variables.IsSet("x"));
    EXPECT_EQ(variables.Get("x"), std::nullopt);

    EXPECT_TRUE(variables.Set("x", "1"));
    EXPECT_TRUE(variables.IsSet("x"));
    EXPECT_EQ(variables.Get("x"), "1");

    EXPECT_TRUE(variables.Set("x", "2"));
    EXPECT_EQ(variables.Get("x"), "2");

    EXPECT_TRUE(variables.Unset("x"));
    EXPECT_FALSE(variables.IsSet("x"));
    EXPECT_EQ(variables.Get("x"), std::nullopt);

    // Unsetting what was not there succeeds; Get of an unknown name is nullopt.
    EXPECT_TRUE(variables.Unset("y"));
    EXPECT_EQ(variables.Get("y"), std::nullopt);
}

TEST(HshVariablesTest, ReadonlyAndExport) {
    ShellVariables variables;
    variables.Set("r", "1");
    variables.MakeReadonly("r");
    EXPECT_TRUE(variables.IsReadonly("r"));
    EXPECT_FALSE(variables.IsReadonly("x"));

    // Set and Unset of a read-only variable fail and change nothing.
    EXPECT_FALSE(variables.Set("r", "2"));
    EXPECT_EQ(variables.Get("r"), "1");
    EXPECT_FALSE(variables.Unset("r"));
    EXPECT_TRUE(variables.IsSet("r"));

    // export NAME of an unset name: it is exported once set.
    variables.Export("e");
    EXPECT_TRUE(variables.IsExported("e"));
    EXPECT_TRUE(variables.ExportedVariables().empty());
    EXPECT_TRUE(variables.Set("e", "v"));
    using NameValue = std::pair<std::string, std::string>;
    EXPECT_EQ(variables.ExportedVariables(), std::vector<NameValue>({NameValue{"e", "v"}}));
    EXPECT_FALSE(variables.IsExported("r"));

    // Names: every set or flagged name, sorted by byte, flagged-but-unset included.
    ShellVariables named;
    named.Set("b", "1");
    named.Set("A", "2");
    named.Export("z");
    named.MakeReadonly("Ba");
    EXPECT_EQ(named.Names(), std::vector<std::string>({"A", "Ba", "b", "z"}));
}

TEST(HshVariablesTest, ImportFromEnvironment) {
    std::shared_ptr<Haisos::IEnvironment> environment = Haisos::CreateEnvironment();
    environment->SetVariable("HOME", "/home/u");
    environment->SetVariable("PATH", "/bin");
    environment->SetVariable("A-B", "skipped"); // not a shell name

    ShellVariables variables;
    variables.ImportFrom(*environment);
    EXPECT_EQ(variables.Get("HOME"), "/home/u");
    EXPECT_EQ(variables.Get("PATH"), "/bin");
    EXPECT_FALSE(variables.IsSet("A-B"));
    EXPECT_FALSE(variables.IsExported("A-B"));

    // Imported variables are exported.
    EXPECT_TRUE(variables.IsExported("HOME"));
    using NameValue = std::pair<std::string, std::string>;
    EXPECT_EQ(variables.ExportedVariables(),
              std::vector<NameValue>({NameValue{"HOME", "/home/u"}, NameValue{"PATH", "/bin"}}));
}

TEST(HshVariablesTest, OptionLetters) {
    EXPECT_EQ(OptionLetters(ShellOptions{}), "");

    ShellOptions on;
    on.errexit = true;
    on.nounset = true;
    on.allexport = true;
    on.noclobber = true;
    on.noglob = true;
    on.xtrace = true;
    EXPECT_EQ(OptionLetters(on), "uaCxfe"); // dash -euaCfx -c 'echo $-'

    ShellOptions interactive;
    interactive.interactive = true;
    EXPECT_EQ(OptionLetters(interactive), "i");
}
