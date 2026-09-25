#include <gtest/gtest.h>
#include <sstream>
#include "src/haisos/HaisosFileParser.h"

using namespace Haisos;

TEST(HaisosFileParserTest, SimpleRunDirective) {
    auto result = ParseHaisosFile("RUN /agent.md\n", {});
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.runEntries.size(), 1u);
    EXPECT_EQ(result.config.runEntries[0].programPath, "/agent.md");
    EXPECT_TRUE(result.config.runEntries[0].args.empty());
}

TEST(HaisosFileParserTest, MultipleRunDirectives) {
    auto result = ParseHaisosFile("RUN /a.md\nRUN /b.lua\n", {});
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.runEntries.size(), 2u);
    EXPECT_EQ(result.config.runEntries[0].programPath, "/a.md");
    EXPECT_EQ(result.config.runEntries[1].programPath, "/b.lua");
}

TEST(HaisosFileParserTest, RunWithArgs) {
    auto result = ParseHaisosFile("RUN /tool.lua foo bar\n", {});
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.runEntries.size(), 1u);
    EXPECT_EQ(result.config.runEntries[0].programPath, "/tool.lua");
    ASSERT_EQ(result.config.runEntries[0].args.size(), 2u);
    EXPECT_EQ(result.config.runEntries[0].args[0], "foo");
    EXPECT_EQ(result.config.runEntries[0].args[1], "bar");
}

TEST(HaisosFileParserTest, RootDirective) {
    auto result = ParseHaisosFile("ROOT ./workspace\nRUN /agent.md\n", {});
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.config.rootPath, "./workspace");
}

TEST(HaisosFileParserTest, DuplicateRootIsError) {
    auto result = ParseHaisosFile("ROOT a\nROOT b\nRUN /agent.md\n", {});
    EXPECT_FALSE(result.error.empty());
}

TEST(HaisosFileParserTest, CommentsAndBlankLinesAreIgnored) {
    auto result = ParseHaisosFile("# a full-line comment\n\nRUN /agent.md # trailing comment\n", {});
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.runEntries.size(), 1u);
    EXPECT_EQ(result.config.runEntries[0].programPath, "/agent.md");
}

TEST(HaisosFileParserTest, ArgUsesDefaultWhenNoOverride) {
    auto result = ParseHaisosFile("ARG name=world\nVAR greeting=Hello-${name}\nRUN /${greeting}.md\n", {});
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.runEntries.size(), 1u);
    EXPECT_EQ(result.config.runEntries[0].programPath, "/Hello-world.md");
}

TEST(HaisosFileParserTest, ArgOverrideWinsOverDefault) {
    auto result = ParseHaisosFile("ARG name=world\nRUN /${name}.md\n", {{"name", "haisos"}});
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.runEntries.size(), 1u);
    EXPECT_EQ(result.config.runEntries[0].programPath, "/haisos.md");
}

TEST(HaisosFileParserTest, VarReferencesArg) {
    auto result = ParseHaisosFile("ARG env=dev\nVAR path=configs/${env}\nRUN /${path}/agent.md\n", {});
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.runEntries.size(), 1u);
    EXPECT_EQ(result.config.runEntries[0].programPath, "/configs/dev/agent.md");
}

TEST(HaisosFileParserTest, NoRunDirectiveIsError) {
    auto result = ParseHaisosFile("ROOT .\n", {});
    EXPECT_FALSE(result.error.empty());
}

TEST(HaisosFileParserTest, UnknownDirectiveIsError) {
    auto result = ParseHaisosFile("FOO bar\nRUN /agent.md\n", {});
    EXPECT_FALSE(result.error.empty());
}

TEST(HaisosFileParserTest, RunMissingPathIsError) {
    auto result = ParseHaisosFile("RUN\n", {});
    EXPECT_FALSE(result.error.empty());
}

TEST(HaisosFileParserTest, VarMissingEqualsIsError) {
    auto result = ParseHaisosFile("VAR novalue\nRUN /agent.md\n", {});
    EXPECT_FALSE(result.error.empty());
}

TEST(HaisosFileParserTest, UnresolvedSubstitutionIsError) {
    auto result = ParseHaisosFile("RUN /${undefined}agent.md\n", {});
    EXPECT_FALSE(result.error.empty());
}

TEST(HaisosFileParserTest, FsPhysicalDirective) {
    auto result = ParseHaisosFile("FS workspace PHYSICAL ./data\nRUN /agent.md\n", {});
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.fsSteps.size(), 1u);
    EXPECT_FALSE(result.config.fsSteps[0].isMount);
    EXPECT_EQ(result.config.fsSteps[0].declare.name, "workspace");
    EXPECT_EQ(result.config.fsSteps[0].declare.type, "PHYSICAL");
    ASSERT_EQ(result.config.fsSteps[0].declare.args.size(), 1u);
    EXPECT_EQ(result.config.fsSteps[0].declare.args[0], "./data");
}

TEST(HaisosFileParserTest, FsMemDirectiveTakesNoArgs) {
    auto result = ParseHaisosFile("FS scratch MEM\nRUN /agent.md\n", {});
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
        "RUN /agent.md\n", {});
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
    auto result = ParseHaisosFile("FS thing BOGUS x\nRUN /agent.md\n", {});
    EXPECT_FALSE(result.error.empty());
}

TEST(HaisosFileParserTest, FsWrongArgCountIsError) {
    auto result = ParseHaisosFile("FS workspace PHYSICAL\nRUN /agent.md\n", {});
    EXPECT_FALSE(result.error.empty());
}

TEST(HaisosFileParserTest, MountDirective) {
    auto result = ParseHaisosFile(
        "FS main PHYSICAL .\n"
        "FS scratch MEM\n"
        "MOUNT main /data scratch\n"
        "RUN /agent.md\n", {});
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.fsSteps.size(), 3u);
    EXPECT_TRUE(result.config.fsSteps[2].isMount);
    EXPECT_EQ(result.config.fsSteps[2].mount.mainFs, "main");
    EXPECT_EQ(result.config.fsSteps[2].mount.path, "/data");
    EXPECT_EQ(result.config.fsSteps[2].mount.toBeMountedFs, "scratch");
}

TEST(HaisosFileParserTest, MountWrongArgCountIsError) {
    auto result = ParseHaisosFile("MOUNT main /data\nRUN /agent.md\n", {});
    EXPECT_FALSE(result.error.empty());
}

TEST(HaisosFileParserTest, RootByNameWhenFsDeclared) {
    auto result = ParseHaisosFile("FS workspace PHYSICAL .\nROOT workspace\nRUN /agent.md\n", {});
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.config.rootPath, "workspace");
}

TEST(HaisosFileParserTest, TemplateParsesCleanly) {
    // GetHaisosFileTemplate({"echo", "ls"}) must itself be a valid haisosfile.
    auto result = ParseHaisosFile(GetHaisosFileTemplate({"echo", "ls"}), {});
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.runEntries.size(), 1u);
    EXPECT_EQ(result.config.runEntries[0].programPath, "/agent.md");
}

TEST(HaisosFileParserTest, EnvSetsValueDirectly) {
    auto result = ParseHaisosFile("ENV GREETING=hello\nRUN /agent.md\n", {});

    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.envEntries.size(), 1u);
    EXPECT_EQ(result.config.envEntries[0].name, "GREETING");
    EXPECT_EQ(result.config.envEntries[0].value, "hello");
    EXPECT_FALSE(result.config.envEntries[0].importFromHost);
}

TEST(HaisosFileParserTest, EnvWithoutValueImportsFromHost) {
    auto result = ParseHaisosFile("ENV HAISOS_MODEL\nRUN /agent.md\n", {});

    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.envEntries.size(), 1u);
    EXPECT_EQ(result.config.envEntries[0].name, "HAISOS_MODEL");
    EXPECT_TRUE(result.config.envEntries[0].importFromHost);
}

TEST(HaisosFileParserTest, EnvValueSupportsSubstitution) {
    auto result = ParseHaisosFile("ARG who=world\nENV GREETING=hi-${who}\nRUN /agent.md\n", {});

    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.config.envEntries.size(), 1u);
    EXPECT_EQ(result.config.envEntries[0].value, "hi-world");
}

TEST(HaisosFileParserTest, EnvRequiresAName) {
    auto result = ParseHaisosFile("ENV\nRUN /agent.md\n", {});

    EXPECT_FALSE(result.error.empty());
}

TEST(HaisosFileParserTest, EnvRejectsEmptyNameBeforeEquals) {
    auto result = ParseHaisosFile("ENV =value\nRUN /agent.md\n", {});

    EXPECT_FALSE(result.error.empty());
}

TEST(HaisosFileParserTest, TemplateDeclaresEnvAndParsesCleanly) {
    // --init must emit a haisosfile that actually parses, including its ENV lines.
    auto result = ParseHaisosFile(GetHaisosFileTemplate({"echo", "ls"}), {});

    EXPECT_TRUE(result.error.empty()) << result.error;
    EXPECT_FALSE(result.config.envEntries.empty());
}

// --- RUN: absolute paths and -i ---

TEST(HaisosFileParserTest, RunRequiresAnAbsolutePath) {
    auto result = ParseHaisosFile("RUN agent.md\n", {});
    EXPECT_NE(result.error.find("absolute"), std::string::npos) << result.error;
}

TEST(HaisosFileParserTest, RunIsNotInteractiveByDefault) {
    auto result = ParseHaisosFile("RUN /agent.md\n", {});
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.config.runEntries.size(), 1u);
    EXPECT_FALSE(result.config.runEntries[0].interactive);
}

TEST(HaisosFileParserTest, RunDashIMakesItInteractive) {
    auto result = ParseHaisosFile("RUN -i /chat.md hello\n", {});
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.config.runEntries.size(), 1u);
    EXPECT_TRUE(result.config.runEntries[0].interactive);
    EXPECT_EQ(result.config.runEntries[0].programPath, "/chat.md");
    ASSERT_EQ(result.config.runEntries[0].args.size(), 1u);
    EXPECT_EQ(result.config.runEntries[0].args[0], "hello");
}

TEST(HaisosFileParserTest, RunDashIAfterTheProgramIsAnArgument) {
    auto result = ParseHaisosFile("RUN /chat.md -i\n", {});
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_FALSE(result.config.runEntries[0].interactive);
    ASSERT_EQ(result.config.runEntries[0].args.size(), 1u);
    EXPECT_EQ(result.config.runEntries[0].args[0], "-i");
}

TEST(HaisosFileParserTest, RunDashIWithoutAProgramIsError) {
    auto result = ParseHaisosFile("RUN -i\n", {});
    EXPECT_FALSE(result.error.empty());
}

// --- CREATE / APPEND ---

namespace {

HaisosFileOperation SingleSetupOperation(const HaisosFileParseResult& result) {
    EXPECT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.config.setupOperations.size(), 1u);
    return result.config.setupOperations.empty() ? HaisosFileOperation{} : result.config.setupOperations[0];
}

} // namespace

TEST(HaisosFileParserTest, CreateWithSingleQuotes) {
    auto op = SingleSetupOperation(ParseHaisosFile("CREATE /a.txt 'say \"hi\" # not a comment'\nRUN /agent.md\n", {}));
    EXPECT_EQ(op.type, HaisosFileOperationType::Create);
    EXPECT_EQ(op.path, "/a.txt");
    EXPECT_EQ(op.content, "say \"hi\" # not a comment");
    EXPECT_EQ(op.lineNumber, 1);
}

TEST(HaisosFileParserTest, AppendWithDoubleQuotesAndTrailingComment) {
    auto op = SingleSetupOperation(ParseHaisosFile("APPEND /a.txt \"it's here\"   # a real comment\nRUN /agent.md\n", {}));
    EXPECT_EQ(op.type, HaisosFileOperationType::Append);
    EXPECT_EQ(op.content, "it's here");
}

TEST(HaisosFileParserTest, CreateWithEmptyQuotes) {
    auto op = SingleSetupOperation(ParseHaisosFile("CREATE /empty.txt ''\nRUN /agent.md\n", {}));
    EXPECT_EQ(op.content, "");
}

TEST(HaisosFileParserTest, CreateUnterminatedQuoteIsError) {
    auto result = ParseHaisosFile("CREATE /a.txt 'oops\nRUN /agent.md\n", {});
    EXPECT_FALSE(result.error.empty());
}

TEST(HaisosFileParserTest, CreateTextAfterClosingQuoteIsError) {
    auto result = ParseHaisosFile("CREATE /a.txt 'one' two\nRUN /agent.md\n", {});
    EXPECT_FALSE(result.error.empty());
}

TEST(HaisosFileParserTest, CreateWithTextTakesTheRestOfTheLineAsIs) {
    auto op = SingleSetupOperation(ParseHaisosFile("CREATE /a.txt text: keep 'this' # and this  \r\nRUN /agent.md\n", {}));
    // Everything after "text:" -- the leading space, the '#', the trailing
    // spaces -- but not the line ending.
    EXPECT_EQ(op.content, " keep 'this' # and this  ");
}

TEST(HaisosFileParserTest, CreateContentIsNotSubstituted) {
    auto op = SingleSetupOperation(ParseHaisosFile("ARG x=1\nCREATE /${x}.txt text:${x} ${nope}\nRUN /agent.md\n", {}));
    EXPECT_EQ(op.path, "/1.txt");
    EXPECT_EQ(op.content, "${x} ${nope}");
}

TEST(HaisosFileParserTest, CreateMultilineStripsTheLastNewline) {
    auto result = ParseHaisosFile(
        "CREATE /poem.md multiline   END  \n"
        "# a heading, not a comment\n"
        "  indented line #\n"
        "\n"
        "last\n"
        "  END \n"
        "RUN /agent.md\n", {});
    auto op = SingleSetupOperation(result);
    EXPECT_EQ(op.content, "# a heading, not a comment\n  indented line #\n\nlast");
    ASSERT_EQ(result.config.runEntries.size(), 1u);
}

TEST(HaisosFileParserTest, CreateMultilineWithAnEmptyLineBeforeTheMarkerKeepsANewline) {
    auto op = SingleSetupOperation(ParseHaisosFile("CREATE /a.txt multiline EOF\nhello\n\nEOF\nRUN /agent.md\n", {}));
    EXPECT_EQ(op.content, "hello\n");
}

TEST(HaisosFileParserTest, CreateMultilineCanBeEmpty) {
    auto op = SingleSetupOperation(ParseHaisosFile("CREATE /a.txt multiline EOF\nEOF\nRUN /agent.md\n", {}));
    EXPECT_EQ(op.content, "");
}

TEST(HaisosFileParserTest, CreateMultilineStripsCarriageReturns) {
    auto op = SingleSetupOperation(ParseHaisosFile("CREATE /a.txt multiline EOF\r\none\r\ntwo\r\nEOF\r\nRUN /agent.md\r\n", {}));
    EXPECT_EQ(op.content, "one\ntwo");
}

TEST(HaisosFileParserTest, CreateMultilineLinesAreNotDirectives) {
    auto result = ParseHaisosFile("CREATE /a.txt multiline EOF\nRUN /inside.md\nEOF\nRUN /agent.md\n", {});
    auto op = SingleSetupOperation(result);
    EXPECT_EQ(op.content, "RUN /inside.md");
    ASSERT_EQ(result.config.runEntries.size(), 1u);
    EXPECT_EQ(result.config.runEntries[0].programPath, "/agent.md");
}

TEST(HaisosFileParserTest, CreateUnterminatedMultilineIsError) {
    auto result = ParseHaisosFile("CREATE /a.txt multiline EOF\nhello\nRUN /agent.md\n", {});
    EXPECT_NE(result.error.find("EOF"), std::string::npos) << result.error;
}

TEST(HaisosFileParserTest, CreateMultilineWithoutAMarkerIsError) {
    auto result = ParseHaisosFile("CREATE /a.txt multiline\nhello\nRUN /agent.md\n", {});
    EXPECT_FALSE(result.error.empty());
}

TEST(HaisosFileParserTest, CreateErrorsAfterMultilineReportTheRightLine) {
    auto result = ParseHaisosFile("CREATE /a.txt multiline EOF\none\ntwo\nEOF\nBOGUS\nRUN /agent.md\n", {});
    EXPECT_NE(result.error.find("line 5"), std::string::npos) << result.error;
}

TEST(HaisosFileParserTest, CreateRequiresAnAbsolutePath) {
    auto result = ParseHaisosFile("CREATE a.txt 'x'\nRUN /agent.md\n", {});
    EXPECT_NE(result.error.find("absolute"), std::string::npos) << result.error;
}

TEST(HaisosFileParserTest, CreateWithoutContentIsError) {
    EXPECT_FALSE(ParseHaisosFile("CREATE /a.txt\nRUN /agent.md\n", {}).error.empty());
    EXPECT_FALSE(ParseHaisosFile("CREATE /a.txt bare words\nRUN /agent.md\n", {}).error.empty());
    EXPECT_FALSE(ParseHaisosFile("CREATE\nRUN /agent.md\n", {}).error.empty());
}

// --- COPY / DELETE / OUTCOPY ---

TEST(HaisosFileParserTest, CopyDeleteAndOutCopy) {
    auto result = ParseHaisosFile(
        "ARG dir=work\n"
        "COPY ./in.txt /${dir}/in.txt\n"
        "DELETE /${dir}/old\n"
        "RUN /agent.md\n"
        "OUTCOPY /${dir}/out.txt ./out.txt # pulled out at the end\n", {});
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.config.setupOperations.size(), 2u);
    EXPECT_EQ(result.config.setupOperations[0].type, HaisosFileOperationType::Copy);
    EXPECT_EQ(result.config.setupOperations[0].hostPath, "./in.txt");
    EXPECT_EQ(result.config.setupOperations[0].path, "/work/in.txt");
    EXPECT_EQ(result.config.setupOperations[1].type, HaisosFileOperationType::Delete);
    EXPECT_EQ(result.config.setupOperations[1].path, "/work/old");
    ASSERT_EQ(result.config.outCopyOperations.size(), 1u);
    EXPECT_EQ(result.config.outCopyOperations[0].type, HaisosFileOperationType::OutCopy);
    EXPECT_EQ(result.config.outCopyOperations[0].path, "/work/out.txt");
    EXPECT_EQ(result.config.outCopyOperations[0].hostPath, "./out.txt");
}

TEST(HaisosFileParserTest, FileDirectivesRequireAbsoluteOsPaths) {
    EXPECT_FALSE(ParseHaisosFile("COPY ./in.txt in.txt\nRUN /agent.md\n", {}).error.empty());
    EXPECT_FALSE(ParseHaisosFile("DELETE old\nRUN /agent.md\n", {}).error.empty());
    EXPECT_FALSE(ParseHaisosFile("RUN /agent.md\nOUTCOPY out.txt ./out.txt\n", {}).error.empty());
}

TEST(HaisosFileParserTest, FileDirectivesCheckTheirArgumentCount) {
    EXPECT_FALSE(ParseHaisosFile("COPY ./in.txt\nRUN /agent.md\n", {}).error.empty());
    EXPECT_FALSE(ParseHaisosFile("DELETE /a /b\nRUN /agent.md\n", {}).error.empty());
    EXPECT_FALSE(ParseHaisosFile("RUN /agent.md\nOUTCOPY /out.txt\n", {}).error.empty());
}

// --- Ordering ---

TEST(HaisosFileParserTest, RootFsAndMountCannotFollowAFileDirective) {
    EXPECT_FALSE(ParseHaisosFile("FS a MEM\nCREATE /x 'y'\nROOT a\nRUN /agent.md\n", {}).error.empty());
    EXPECT_FALSE(ParseHaisosFile("FS a MEM\nDELETE /x\nFS b MEM\nRUN /agent.md\n", {}).error.empty());
    EXPECT_FALSE(ParseHaisosFile("FS a MEM\nFS b MEM\nCOPY ./x /x\nMOUNT a /b b\nRUN /agent.md\n", {}).error.empty());
    EXPECT_FALSE(ParseHaisosFile("FS a MEM\nRUN /agent.md\nOUTCOPY /x ./x\nROOT a\n", {}).error.empty());
}

TEST(HaisosFileParserTest, SetupFileDirectivesMustComeBeforeRun) {
    auto result = ParseHaisosFile("RUN /agent.md\nCREATE /x 'y'\n", {});
    EXPECT_NE(result.error.find("before any process"), std::string::npos) << result.error;
}

TEST(HaisosFileParserTest, OutCopyMayComeBeforeRun) {
    auto result = ParseHaisosFile("OUTCOPY /x ./x\nRUN /agent.md\n", {});
    EXPECT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.config.outCopyOperations.size(), 1u);
}

TEST(HaisosFileParserTest, TemplateFsExamplesParseOnceUncommented) {
    // Every commented-out "# FS ..." example in the template must turn into a
    // valid declaration -- note included -- just by deleting its leading '#'.
    std::string declarations = "FS rootfs PHYSICAL .\n";
    std::istringstream lines(GetHaisosFileTemplate({"echo", "ls"}));
    std::string line;
    int examples = 0;
    while (std::getline(lines, line)) {
        if (line.rfind("# FS ", 0) == 0 && line.find(" # ") != std::string::npos) {
            // In dependency order: the scratch example is used by COMPOSED.
            declarations += line.substr(2) + "\n";
            ++examples;
        }
    }
    EXPECT_GE(examples, 5);
    auto result = ParseHaisosFile(declarations + "RUN /agent.md\n", {});
    EXPECT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.config.fsSteps.size(), static_cast<size_t>(examples) + 1);
}

TEST(HaisosFileParserTest, DevFilesystemTakesNoArguments) {
    auto result = ParseHaisosFile("FS devfs DEV\nRUN /agent.md\n", {});
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.config.fsSteps.size(), 1u);
    EXPECT_EQ(result.config.fsSteps[0].declare.type, "DEV");
    EXPECT_TRUE(result.config.fsSteps[0].declare.args.empty());

    EXPECT_FALSE(ParseHaisosFile("FS devfs DEV /dev\nRUN /agent.md\n", {}).error.empty());
}

// --- CREATE_DIR and BUILTIN ---

TEST(HaisosFileParserTest, CreateDirTakesOneAbsolutePath) {
    auto result = ParseHaisosFile("CREATE_DIR /bin\nRUN /agent.md\n", {});
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.config.setupOperations.size(), 1u);
    EXPECT_EQ(result.config.setupOperations[0].type, HaisosFileOperationType::CreateDir);
    EXPECT_EQ(result.config.setupOperations[0].path, "/bin");

    EXPECT_FALSE(ParseHaisosFile("CREATE_DIR bin\nRUN /agent.md\n", {}).error.empty());
    EXPECT_FALSE(ParseHaisosFile("CREATE_DIR /a /b\nRUN /agent.md\n", {}).error.empty());
    EXPECT_FALSE(ParseHaisosFile("CREATE_DIR\nRUN /agent.md\n", {}).error.empty());
}

TEST(HaisosFileParserTest, BuiltinBecomesOneOperationPerPath) {
    auto result = ParseHaisosFile("ARG where=/usr/bin\nBUILTIN rootfs ls /bin/ls ${where}/ls\nRUN /agent.md\n", {});
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.config.setupOperations.size(), 2u);
    for (const auto& operation : result.config.setupOperations) {
        EXPECT_EQ(operation.type, HaisosFileOperationType::Builtin);
        EXPECT_EQ(operation.fsName, "rootfs");
        EXPECT_EQ(operation.builtinName, "ls");
        EXPECT_EQ(operation.lineNumber, 2);
    }
    EXPECT_EQ(result.config.setupOperations[0].path, "/bin/ls");
    EXPECT_EQ(result.config.setupOperations[1].path, "/usr/bin/ls");
}

TEST(HaisosFileParserTest, BuiltinNeedsAFilesystemANameAndAbsolutePaths) {
    EXPECT_FALSE(ParseHaisosFile("BUILTIN rootfs ls\nRUN /agent.md\n", {}).error.empty());
    EXPECT_FALSE(ParseHaisosFile("BUILTIN rootfs\nRUN /agent.md\n", {}).error.empty());
    auto relative = ParseHaisosFile("BUILTIN rootfs ls bin/ls\nRUN /agent.md\n", {});
    EXPECT_NE(relative.error.find("absolute"), std::string::npos) << relative.error;
}

TEST(HaisosFileParserTest, BuiltinAndCreateDirFollowTheFileDirectiveOrdering) {
    // After FS/MOUNT/ROOT, before the first RUN.
    EXPECT_FALSE(ParseHaisosFile("BUILTIN rootfs ls /ls\nFS rootfs MEM\nRUN /agent.md\n", {}).error.empty());
    EXPECT_FALSE(ParseHaisosFile("CREATE_DIR /bin\nROOT x\nRUN /agent.md\n", {}).error.empty());
    EXPECT_FALSE(ParseHaisosFile("RUN /agent.md\nBUILTIN rootfs ls /ls\n", {}).error.empty());
    EXPECT_FALSE(ParseHaisosFile("RUN /agent.md\nCREATE_DIR /bin\n", {}).error.empty());
}

TEST(HaisosFileParserTest, TemplateNamesTheRootRootfsAndListsEveryBuiltin) {
    const std::vector<std::string> builtins = {"cat", "echo", "ls"};
    const std::string haisosfile = GetHaisosFileTemplate(builtins);
    EXPECT_NE(haisosfile.find("\nFS rootfs PHYSICAL ."), std::string::npos);
    EXPECT_NE(haisosfile.find("\nROOT rootfs\n"), std::string::npos);
    EXPECT_NE(haisosfile.find("\n# CREATE_DIR /bin\n"), std::string::npos);
    for (const auto& name : builtins) {
        EXPECT_NE(haisosfile.find("\n# BUILTIN rootfs " + name + " /bin/" + name + "\n"), std::string::npos) << name;
    }
    // CREATE_DIR /bin comes after ROOT and before the BUILTINs that need it.
    EXPECT_LT(haisosfile.find("\nROOT rootfs\n"), haisosfile.find("# CREATE_DIR /bin"));
    EXPECT_LT(haisosfile.find("# CREATE_DIR /bin"), haisosfile.find("# BUILTIN rootfs cat"));
}

TEST(HaisosFileParserTest, TemplateBuiltinExamplesParseOnceUncommented) {
    // Uncommenting CREATE_DIR /bin and every BUILTIN, in place, still gives a
    // valid haisosfile.
    std::istringstream lines(GetHaisosFileTemplate({"cat", "echo"}));
    std::string haisosfile;
    std::string line;
    int builtins = 0;
    while (std::getline(lines, line)) {
        if (line == "# CREATE_DIR /bin" || line.rfind("# BUILTIN rootfs ", 0) == 0) {
            line = line.substr(2);
            builtins += (line.rfind("BUILTIN", 0) == 0) ? 1 : 0;
        }
        haisosfile += line + "\n";
    }
    EXPECT_EQ(builtins, 2);
    auto result = ParseHaisosFile(haisosfile, {});
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.config.setupOperations.size(), 3u);
    EXPECT_EQ(result.config.setupOperations[0].type, HaisosFileOperationType::CreateDir);
    EXPECT_EQ(result.config.setupOperations[1].type, HaisosFileOperationType::Builtin);
}
