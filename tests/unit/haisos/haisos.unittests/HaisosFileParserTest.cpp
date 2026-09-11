#include <gtest/gtest.h>
#include "src/haisos/HaisosFileParser.h"

using namespace Haisos;

TEST(HaisosFileParserTest, SimpleRunDirective) {
    auto result = ParseHaisosFile("RUN agent.md\n", {});
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.runEntries.size(), 1u);
    EXPECT_EQ(result.config.runEntries[0].programPath, "agent.md");
    EXPECT_TRUE(result.config.runEntries[0].args.empty());
}

TEST(HaisosFileParserTest, MultipleRunDirectives) {
    auto result = ParseHaisosFile("RUN a.md\nRUN b.lua\n", {});
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.runEntries.size(), 2u);
    EXPECT_EQ(result.config.runEntries[0].programPath, "a.md");
    EXPECT_EQ(result.config.runEntries[1].programPath, "b.lua");
}

TEST(HaisosFileParserTest, RunWithArgs) {
    auto result = ParseHaisosFile("RUN tool.lua foo bar\n", {});
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.runEntries.size(), 1u);
    EXPECT_EQ(result.config.runEntries[0].programPath, "tool.lua");
    ASSERT_EQ(result.config.runEntries[0].args.size(), 2u);
    EXPECT_EQ(result.config.runEntries[0].args[0], "foo");
    EXPECT_EQ(result.config.runEntries[0].args[1], "bar");
}

TEST(HaisosFileParserTest, RootDirective) {
    auto result = ParseHaisosFile("ROOT ./workspace\nRUN agent.md\n", {});
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.config.rootPath, "./workspace");
}

TEST(HaisosFileParserTest, DuplicateRootIsError) {
    auto result = ParseHaisosFile("ROOT a\nROOT b\nRUN agent.md\n", {});
    EXPECT_FALSE(result.error.empty());
}

TEST(HaisosFileParserTest, CommentsAndBlankLinesAreIgnored) {
    auto result = ParseHaisosFile("# a full-line comment\n\nRUN agent.md # trailing comment\n", {});
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.runEntries.size(), 1u);
    EXPECT_EQ(result.config.runEntries[0].programPath, "agent.md");
}

TEST(HaisosFileParserTest, ArgUsesDefaultWhenNoOverride) {
    auto result = ParseHaisosFile("ARG name=world\nVAR greeting=Hello-${name}\nRUN ${greeting}.md\n", {});
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.runEntries.size(), 1u);
    EXPECT_EQ(result.config.runEntries[0].programPath, "Hello-world.md");
}

TEST(HaisosFileParserTest, ArgOverrideWinsOverDefault) {
    auto result = ParseHaisosFile("ARG name=world\nRUN ${name}.md\n", {{"name", "haisos"}});
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.runEntries.size(), 1u);
    EXPECT_EQ(result.config.runEntries[0].programPath, "haisos.md");
}

TEST(HaisosFileParserTest, VarReferencesArg) {
    auto result = ParseHaisosFile("ARG env=dev\nVAR path=configs/${env}\nRUN ${path}/agent.md\n", {});
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.runEntries.size(), 1u);
    EXPECT_EQ(result.config.runEntries[0].programPath, "configs/dev/agent.md");
}

TEST(HaisosFileParserTest, NoRunDirectiveIsError) {
    auto result = ParseHaisosFile("ROOT .\n", {});
    EXPECT_FALSE(result.error.empty());
}

TEST(HaisosFileParserTest, UnknownDirectiveIsError) {
    auto result = ParseHaisosFile("FOO bar\nRUN agent.md\n", {});
    EXPECT_FALSE(result.error.empty());
}

TEST(HaisosFileParserTest, RunMissingPathIsError) {
    auto result = ParseHaisosFile("RUN\n", {});
    EXPECT_FALSE(result.error.empty());
}

TEST(HaisosFileParserTest, VarMissingEqualsIsError) {
    auto result = ParseHaisosFile("VAR novalue\nRUN agent.md\n", {});
    EXPECT_FALSE(result.error.empty());
}

TEST(HaisosFileParserTest, UnresolvedSubstitutionBecomesEmpty) {
    auto result = ParseHaisosFile("RUN ${undefined}agent.md\n", {});
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.runEntries.size(), 1u);
    EXPECT_EQ(result.config.runEntries[0].programPath, "agent.md");
}
