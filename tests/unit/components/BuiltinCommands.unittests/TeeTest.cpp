#include <gtest/gtest.h>
#include <string>

#include "BuiltinCommandsFixture.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/libheaders/ExitCodes.h"

using namespace Haisos;

namespace {

const std::string kInput = "one\ntwo\nthree\n";

} // namespace

// --- tee ---

TEST_F(BuiltinCommandsTest, TeeCopiesToStandardOutput) {
    const Captured captured = RunCaptured("tee", {}, kInput);
    EXPECT_EQ(captured.out, kInput);
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, TeeWritesFiles) {
    const Captured captured = RunCaptured("tee", {"t1", "t2"}, kInput);
    EXPECT_EQ(captured.out, kInput);
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    std::string content;
    ASSERT_TRUE(ReadWholeFile(*root, "/t1", content));
    EXPECT_EQ(content, kInput);
    ASSERT_TRUE(ReadWholeFile(*root, "/t2", content));
    EXPECT_EQ(content, kInput);
}

TEST_F(BuiltinCommandsTest, TeeTruncatesByDefaultAndAppendsWithA) {
    WriteFile("/t1", "old contents\n");
    WriteFile("/t2", "old contents\n");
    const Captured overwrite = RunCaptured("tee", {"t1"}, kInput);
    EXPECT_EQ(overwrite.out, kInput);
    EXPECT_EQ(overwrite.status, 0);
    const Captured append = RunCaptured("tee", {"-a", "t2"}, kInput);
    EXPECT_EQ(append.out, kInput);
    EXPECT_EQ(append.status, 0);
    std::string content;
    ASSERT_TRUE(ReadWholeFile(*root, "/t1", content));
    EXPECT_EQ(content, kInput);
    ASSERT_TRUE(ReadWholeFile(*root, "/t2", content));
    EXPECT_EQ(content, "old contents\n" + kInput);
}

// A missing parent directory is an open failure gone past, as GNU's: tee
// creates no directories.
TEST_F(BuiltinCommandsTest, TeeAppendIntoAMissingDirectoryFails) {
    const Captured captured = RunCaptured("tee", {"--append", "/made/up.txt"}, kInput);
    EXPECT_EQ(captured.out, kInput);
    EXPECT_EQ(captured.err, "tee: /made/up.txt: No such file or directory\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, TeeOpenFailuresAreReportedAndGonePast) {
    const Captured captured = RunCaptured("tee", {"/docs", "/nodir/x", "t1"}, kInput);
    // Standard output still got everything, and the readable file was written.
    EXPECT_EQ(captured.out, kInput);
    EXPECT_EQ(captured.err,
        "tee: /docs: Is a directory\n"
        "tee: /nodir/x: No such file or directory\n");
    EXPECT_EQ(captured.status, 1);
    std::string content;
    ASSERT_TRUE(ReadWholeFile(*root, "/t1", content));
    EXPECT_EQ(content, kInput);
}

// A "-" operand is a file's name, as GNU takes it -- not the standard input.
TEST_F(BuiltinCommandsTest, TeeDashIsAFileName) {
    const Captured captured = RunCaptured("tee", {"-"}, kInput);
    EXPECT_EQ(captured.out, kInput);
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    std::string content;
    ASSERT_TRUE(ReadWholeFile(*root, "/-", content));
    EXPECT_EQ(content, kInput);
}

TEST_F(BuiltinCommandsTest, TeeOutputErrorTakesTheModes) {
    Captured captured = RunCaptured("tee", {"--output-error=foo"}, kInput);
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err,
        "tee: invalid argument 'foo' for '--output-error'\n"
        "Valid arguments are:\n"
        "  - 'warn'\n"
        "  - 'warn-nopipe'\n"
        "  - 'exit'\n"
        "  - 'exit-nopipe'\n"
        "Try 'tee --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("tee", {"--output-error=e"}, kInput);
    EXPECT_EQ(captured.err,
        "tee: ambiguous argument 'e' for '--output-error'\n"
        "Valid arguments are:\n"
        "  - 'warn'\n"
        "  - 'warn-nopipe'\n"
        "  - 'exit'\n"
        "  - 'exit-nopipe'\n"
        "Try 'tee --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);

    // An unambiguous prefix, as getopt_long allows.
    captured = RunCaptured("tee", {"--output-error=warn-n"}, kInput);
    EXPECT_EQ(captured.out, kInput);
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, TeeIgnoreInterruptsIsNotTreated) {
    const Captured captured = RunCaptured("tee", {"-i"}, kInput);
    EXPECT_EQ(captured.out, kInput);
    EXPECT_EQ(captured.err,
        "Parameter -i is not treated by HaisosOS tee v. 1.0.0\n");
    EXPECT_EQ(captured.status, 0);
}

// --- tee against a standard output nobody reads ---

namespace {

// Standard input holding kInput, for a run whose stdout is a pipe with no
// reader: without it tee would read an input that ends at once and never
// write, so no pipe would ever break.
std::shared_ptr<IFileDescriptor> MakeStdIn(const std::shared_ptr<IFileSystem>& streams,
                                           const std::string& input) {
    auto file = streams->OpenFile("/teein", kFileOpenWriteCreateTruncate, kFileCreateMode);
    EXPECT_NE(file, nullptr);
    if (file) {
        file->Write(input.data(), input.size());
    }
    file.reset();
    return streams->OpenFile("/teein", kFileOpenReadOnly);
}

} // namespace

TEST_F(BuiltinCommandsTest, TeeBrokenPipeDefaultExits141Quietly) {
    auto ends = os->GetPipeService()->CreatePipe();
    ends.readEnd.reset();
    StartProcessOptions options;
    options.stdIn = MakeStdIn(streams, kInput);
    options.stdOut = ends.writeEnd;
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/tee", {}, "/", options);
    ends.writeEnd.reset();
    options.stdOut.reset();
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->WaitToFinish(kWaitMs));
    ASSERT_TRUE(process->ExitCode().has_value());
    EXPECT_EQ(*process->ExitCode(), kExitCodeBrokenPipe);
    EXPECT_EQ(console->TakeOut(), "");
    EXPECT_EQ(console->TakeErr(), "");
}

TEST_F(BuiltinCommandsTest, TeeBrokenPipeWarnDiagnosesAndFinishesTheFile) {
    auto ends = os->GetPipeService()->CreatePipe();
    ends.readEnd.reset();
    StartProcessOptions options;
    options.stdIn = MakeStdIn(streams, kInput);
    options.stdOut = ends.writeEnd;
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/tee",
        {"--output-error=warn", "wf"}, "/", options);
    ends.writeEnd.reset();
    options.stdOut.reset();
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->WaitToFinish(kWaitMs));
    ASSERT_TRUE(process->ExitCode().has_value());
    EXPECT_EQ(*process->ExitCode(), 1);
    EXPECT_EQ(console->TakeOut(), "");
    EXPECT_EQ(console->TakeErr(), "tee: 'standard output': Broken pipe\n");
    std::string content;
    ASSERT_TRUE(ReadWholeFile(*root, "/wf", content));
    EXPECT_EQ(content, kInput);
}

TEST_F(BuiltinCommandsTest, TeeBrokenPipeNopipeModesDropItSilently) {
    for (const std::string mode : {"-p", "--output-error=warn-nopipe", "--output-error"}) {
        auto ends = os->GetPipeService()->CreatePipe();
        ends.readEnd.reset();
        StartProcessOptions options;
        options.stdIn = MakeStdIn(streams, kInput);
        options.stdOut = ends.writeEnd;
        auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/tee", {mode}, "/",
                                        options);
        ends.writeEnd.reset();
        options.stdOut.reset();
        ASSERT_NE(process, nullptr);
        ASSERT_TRUE(process->WaitToFinish(kWaitMs)) << mode;
        ASSERT_TRUE(process->ExitCode().has_value()) << mode;
        EXPECT_EQ(*process->ExitCode(), 0) << mode;
        EXPECT_EQ(console->TakeOut(), "") << mode;
        EXPECT_EQ(console->TakeErr(), "") << mode;
    }
}

TEST_F(BuiltinCommandsTest, TeeBrokenPipeExitModes) {
    for (const std::string mode : {"--output-error=exit", "--output-error=exit-nopipe"}) {
        auto ends = os->GetPipeService()->CreatePipe();
        ends.readEnd.reset();
        StartProcessOptions options;
        options.stdIn = MakeStdIn(streams, kInput);
        options.stdOut = ends.writeEnd;
        auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/tee", {mode}, "/",
                                        options);
        ends.writeEnd.reset();
        options.stdOut.reset();
        ASSERT_NE(process, nullptr);
        ASSERT_TRUE(process->WaitToFinish(kWaitMs)) << mode;
        ASSERT_TRUE(process->ExitCode().has_value()) << mode;
        if (mode == "--output-error=exit") {
            // The write error is diagnosed, and tee stops on it.
            EXPECT_EQ(*process->ExitCode(), 1) << mode;
            EXPECT_EQ(console->TakeErr(), "tee: 'standard output': Broken pipe\n") << mode;
        } else {
            // A broken pipe is gone past silently, and tee finishes.
            EXPECT_EQ(*process->ExitCode(), 0) << mode;
            EXPECT_EQ(console->TakeErr(), "") << mode;
        }
        EXPECT_EQ(console->TakeOut(), "") << mode;
    }
}
