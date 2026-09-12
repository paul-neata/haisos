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

TEST(HaisosFileParserTest, UnresolvedSubstitutionIsError) {
    auto result = ParseHaisosFile("RUN ${undefined}agent.md\n", {});
    EXPECT_FALSE(result.error.empty());
}

TEST(HaisosFileParserTest, FsPhysicalDirective) {
    auto result = ParseHaisosFile("FS workspace PHYSICAL ./data\nRUN agent.md\n", {});
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.fsSteps.size(), 1u);
    EXPECT_FALSE(result.config.fsSteps[0].isMount);
    EXPECT_EQ(result.config.fsSteps[0].declare.name, "workspace");
    EXPECT_EQ(result.config.fsSteps[0].declare.type, "PHYSICAL");
    ASSERT_EQ(result.config.fsSteps[0].declare.args.size(), 1u);
    EXPECT_EQ(result.config.fsSteps[0].declare.args[0], "./data");
}

TEST(HaisosFileParserTest, FsMemDirectiveTakesNoArgs) {
    auto result = ParseHaisosFile("FS scratch MEM\nRUN agent.md\n", {});
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.fsSteps.size(), 1u);
    EXPECT_EQ(result.config.fsSteps[0].declare.type, "MEM");
    EXPECT_TRUE(result.config.fsSteps[0].declare.args.empty());
}

TEST(HaisosFileParserTest, FsRoAndSubDirectives) {
    auto result = ParseHaisosFile(
        "FS workspace PHYSICAL .\n"
        "FS readonly RO workspace\n"
        "FS inner SUB workspace sub/dir\n"
        "RUN agent.md\n", {});
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.fsSteps.size(), 3u);
    EXPECT_EQ(result.config.fsSteps[1].declare.type, "RO");
    EXPECT_EQ(result.config.fsSteps[1].declare.args[0], "workspace");
    EXPECT_EQ(result.config.fsSteps[2].declare.type, "SUB");
    ASSERT_EQ(result.config.fsSteps[2].declare.args.size(), 2u);
    EXPECT_EQ(result.config.fsSteps[2].declare.args[0], "workspace");
    EXPECT_EQ(result.config.fsSteps[2].declare.args[1], "sub/dir");
}

TEST(HaisosFileParserTest, FsUnknownTypeIsError) {
    auto result = ParseHaisosFile("FS thing BOGUS x\nRUN agent.md\n", {});
    EXPECT_FALSE(result.error.empty());
}

TEST(HaisosFileParserTest, FsWrongArgCountIsError) {
    auto result = ParseHaisosFile("FS workspace PHYSICAL\nRUN agent.md\n", {});
    EXPECT_FALSE(result.error.empty());
}

TEST(HaisosFileParserTest, MountDirective) {
    auto result = ParseHaisosFile(
        "FS main PHYSICAL .\n"
        "FS scratch MEM\n"
        "MOUNT main /data scratch\n"
        "RUN agent.md\n", {});
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.fsSteps.size(), 3u);
    EXPECT_TRUE(result.config.fsSteps[2].isMount);
    EXPECT_EQ(result.config.fsSteps[2].mount.mainFs, "main");
    EXPECT_EQ(result.config.fsSteps[2].mount.path, "/data");
    EXPECT_EQ(result.config.fsSteps[2].mount.toBeMountedFs, "scratch");
}

TEST(HaisosFileParserTest, MountWrongArgCountIsError) {
    auto result = ParseHaisosFile("MOUNT main /data\nRUN agent.md\n", {});
    EXPECT_FALSE(result.error.empty());
}

TEST(HaisosFileParserTest, RootByNameWhenFsDeclared) {
    auto result = ParseHaisosFile("FS workspace PHYSICAL .\nROOT workspace\nRUN agent.md\n", {});
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.config.rootPath, "workspace");
}

TEST(HaisosFileParserTest, TemplateParsesCleanly) {
    // GetHaisosFileTemplate() must itself be a valid haisosfile.
    auto result = ParseHaisosFile(GetHaisosFileTemplate(), {});
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.runEntries.size(), 1u);
    EXPECT_EQ(result.config.runEntries[0].programPath, "agent.md");
}
