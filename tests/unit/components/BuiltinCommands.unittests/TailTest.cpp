#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "BuiltinCommandsFixture.h"

namespace Haisos {
namespace {

// Polls |fs| for |path| until its content ends with |suffix| (10 ms steps, up
// to kWaitMs -- generous, never a fixed sleep). |content| ends holding the
// file's content either way, so a failure can report what was there.
bool WaitEndsWith(IFileSystem& fs, const std::string& path, const std::string& suffix,
                  std::string& content) {
    for (int i = 0; i < static_cast<int>(kWaitMs / 10); ++i) {
        content.clear();
        if (ReadWholeFile(fs, path, content)
            && content.size() >= suffix.size()
            && content.compare(content.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

} // namespace

TEST_F(BuiltinCommandsTest, TailLinesAndBytes) {
    WriteFile("/n12", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n");
    WriteFile("/abc", "a\nb\nc");
    EXPECT_EQ(RunCaptured("tail", {"-3", "/n12"}).out, "10\n11\n12\n");
    EXPECT_EQ(RunCaptured("tail", {"-n", "+10", "/n12"}).out, "10\n11\n12\n");
    EXPECT_EQ(RunCaptured("tail", {"+11", "/n12"}).out, "11\n12\n");
    EXPECT_EQ(RunCaptured("tail", {"-c", "5", "/n12"}).out, "1\n12\n");
    EXPECT_EQ(RunCaptured("tail", {"-c", "+25", "/n12"}).out, "12\n");
    EXPECT_EQ(RunCaptured("tail", {"-2c", "/n12"}).out, "2\n");
    EXPECT_EQ(RunCaptured("tail", {"-n", "2", "/abc"}).out, "b\nc");
    EXPECT_EQ(RunCaptured("tail", {"-n", "0", "/n12"}).out, "");
    EXPECT_EQ(RunCaptured("tail", {"-n", "+0", "/abc"}).out, "a\nb\nc");
    // The obsolete form with no number: 10 of the unit, so -b is 5120 bytes.
    EXPECT_EQ(RunCaptured("tail", {"-b", "/n12"}).out,
        "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n");
    // "-c" alone is the regular option, so the FILE is its missing argument.
    Captured regularC = RunCaptured("tail", {"-c", "/n12"});
    EXPECT_EQ(regularC.err, "tail: invalid number of bytes: '/n12'\n");
    EXPECT_EQ(regularC.status, 1);
    EXPECT_EQ(RunCaptured("tail", {"-z", "-n", "2"}, std::string("x\0y\0z", 5)).out,
        std::string("y\0z", 3));
    // No FILE: the standard input.
    EXPECT_EQ(RunCaptured("tail", {"-n", "1"}, "1\n2\n3\n").out, "3\n");
}

TEST_F(BuiltinCommandsTest, TailHeaders) {
    WriteFile("/n12", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n");
    WriteFile("/abc", "a\nb\nc");
    // A header before each file with more than one FILE; the blank line
    // before the second makes up for the undelimited "c".
    EXPECT_EQ(RunCaptured("tail", {"-n", "1", "/abc", "/n12"}).out,
        "==> /abc <==\nc\n==> /n12 <==\n12\n");
    EXPECT_EQ(RunCaptured("tail", {"-q", "-n1", "/abc", "/n12"}).out, "c12\n");
    EXPECT_EQ(RunCaptured("tail", {"-v", "-n1", "/abc"}).out, "==> /abc <==\nc");
    EXPECT_EQ(RunCaptured("tail", {"-n1", "-", "/n12"}, "a\nb\n").out,
        "==> standard input <==\nb\n\n==> /n12 <==\n12\n");
}

TEST_F(BuiltinCommandsTest, TailErrorsAndWarnings) {
    WriteFile("/n12", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n");
    Captured captured = RunCaptured("tail", {"-n", "1", "/missing", "/n12"});
    EXPECT_EQ(captured.out, "==> /n12 <==\n12\n");
    EXPECT_EQ(captured.err, "tail: cannot open '/missing' for reading: No such file or directory\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("tail", {"-n1", "/n12", "/docs"});
    EXPECT_EQ(captured.out, "==> /n12 <==\n12\n\n==> /docs <==\n");
    EXPECT_EQ(captured.err, "tail: error reading '/docs': Is a directory\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("tail", {"-n", "x", "/n12"});
    EXPECT_EQ(captured.err, "tail: invalid number of lines: 'x'\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("tail", {"-c", "q", "/n12"});
    EXPECT_EQ(captured.err, "tail: invalid number of bytes: 'q'\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("tail", {"-n", "99999999999999999999", "/n12"});
    EXPECT_EQ(captured.err,
        "tail: invalid number of lines: '99999999999999999999': Value too large for defined data type\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("tail", {"-s", "x", "-f", "/n12"});
    EXPECT_EQ(captured.err, "tail: invalid number of seconds: 'x'\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("tail", {"-f", "--follow=bogus", "/n12"});
    EXPECT_EQ(captured.err,
        "tail: invalid argument 'bogus' for '--follow'\n"
        "Valid arguments are:\n"
        "  - 'descriptor'\n"
        "  - 'name'\n"
        "Try 'tail --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);

    // --pid without -f is reported and ignored; the command still succeeds.
    captured = RunCaptured("tail", {"-n2", "--pid=1", "/n12"});
    EXPECT_EQ(captured.out, "11\n12\n");
    EXPECT_EQ(captured.err, "tail: warning: PID ignored; --pid=PID is useful only when following\n");
    EXPECT_EQ(captured.status, 0);

    // So is --retry.
    captured = RunCaptured("tail", {"--retry", "-n1", "/n12"});
    EXPECT_EQ(captured.out, "12\n");
    EXPECT_EQ(captured.err, "tail: warning: --retry ignored; --retry is useful only when following\n");
    EXPECT_EQ(captured.status, 0);

    // A -NUM where the obsolete form is not looked for.
    captured = RunCaptured("tail", {"-n", "1", "-1"});
    EXPECT_EQ(captured.err, "tail: option used in invalid context -- 1\n");
    EXPECT_EQ(captured.status, 1);

    // The obsolete spelling that overflows uintmax_t.
    captured = RunCaptured("tail", {"+99999999999999999999", "/n12"});
    EXPECT_EQ(captured.err,
        "tail: invalid number: '+99999999999999999999': Numerical result out of range\n");
    EXPECT_EQ(captured.status, 1);

    // A FILE that never opens leaves nothing to follow.
    captured = RunCaptured("tail", {"-f", "-n1", "/missing"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err,
        "tail: cannot open '/missing' for reading: No such file or directory\n"
        "tail: no files remaining\n");
    EXPECT_EQ(captured.status, 1);

    // The standard input is read to its end and not followed.
    captured = RunCaptured("tail", {"-f"}, "hi\n");
    EXPECT_EQ(captured.out, "hi\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

// --- Following: the tail process is started directly, its standard streams
// connected to files on the streams filesystem, and polled from the test. ---

TEST_F(BuiltinCommandsTest, TailFollowsAppendedData) {
    WriteFile("/g", "1\n2\n");
    StartProcessOptions options;
    options.stdOut = streams->OpenFile("/tailout", kFileOpenWriteCreateTruncate, kFileCreateMode);
    options.stdErr = streams->OpenFile("/tailerr", kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_NE(options.stdOut, nullptr);
    ASSERT_NE(options.stdErr, nullptr);
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/tail",
        {"-s", "0.01", "-f", "-n1", "/g"}, "/", options);
    ASSERT_NE(process, nullptr);

    std::string content;
    ASSERT_TRUE(WaitEndsWith(*streams, "/tailout", "2\n", content)) << content;

    auto appender = root->OpenFile("/g", kFileOpenWriteCreateAppend, kFileCreateMode);
    ASSERT_NE(appender, nullptr);
    ASSERT_EQ(appender->Write("3\n", 2), 2);
    appender.reset();
    ASSERT_TRUE(WaitEndsWith(*streams, "/tailout", "2\n3\n", content)) << content;

    // A file that shrinks is reopened and printed from its start.
    WriteFile("/g", "x\n");
    ASSERT_TRUE(WaitEndsWith(*streams, "/tailerr", "tail: /g: file truncated\n", content)) << content;
    ASSERT_TRUE(WaitEndsWith(*streams, "/tailout", "x\n", content)) << content;

    process->TriggerStop();
    EXPECT_TRUE(process->WaitToFinish(kWaitMs));
    ASSERT_TRUE(process->ExitCode().has_value());
    EXPECT_EQ(process->ExitCode().value(), 143);
}

TEST_F(BuiltinCommandsTest, TailFollowNameNoticesRemovalAndReappearance) {
    WriteFile("/g", "1\n2\n");
    StartProcessOptions options;
    options.stdOut = streams->OpenFile("/tailout", kFileOpenWriteCreateTruncate, kFileCreateMode);
    options.stdErr = streams->OpenFile("/tailerr", kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_NE(options.stdOut, nullptr);
    ASSERT_NE(options.stdErr, nullptr);
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/tail",
        {"-F", "-s", "0.01", "-n1", "/g"}, "/", options);
    ASSERT_NE(process, nullptr);

    std::string content;
    ASSERT_TRUE(WaitEndsWith(*streams, "/tailout", "2\n", content)) << content;

    ASSERT_EQ(root->RemoveFile("/g"), 0);
    ASSERT_TRUE(WaitEndsWith(*streams, "/tailerr",
        "tail: '/g' has become inaccessible: No such file or directory\n", content)) << content;

    WriteFile("/g", "back\n");
    ASSERT_TRUE(WaitEndsWith(*streams, "/tailerr",
        "tail: '/g' has appeared;  following new file\n", content)) << content;
    ASSERT_TRUE(WaitEndsWith(*streams, "/tailout", "back\n", content)) << content;

    process->TriggerStop();
    EXPECT_TRUE(process->WaitToFinish(kWaitMs));
    ASSERT_TRUE(process->ExitCode().has_value());
    EXPECT_EQ(process->ExitCode().value(), 143);
}

TEST_F(BuiltinCommandsTest, TailFollowEndsWithThePidProcess) {
    StartProcessOptions plain;
    auto watched = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/tail",
        {"-f", "/notes.txt"}, "/", plain);
    ASSERT_NE(watched, nullptr);

    StartProcessOptions options;
    options.stdOut = streams->OpenFile("/tailout", kFileOpenWriteCreateTruncate, kFileCreateMode);
    options.stdErr = streams->OpenFile("/tailerr", kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_NE(options.stdOut, nullptr);
    ASSERT_NE(options.stdErr, nullptr);
    auto follower = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/tail",
        {"-s", "0.01", "-f", "--pid=" + std::to_string(watched->GetPid()), "/notes.txt"}, "/",
        options);
    ASSERT_NE(follower, nullptr);

    // While the --pid process runs, the follower keeps following.
    std::string content;
    ASSERT_TRUE(WaitEndsWith(*streams, "/tailout", "\tthree\n", content)) << content;
    EXPECT_FALSE(follower->WaitToFinish(200));

    watched->TriggerStop();
    EXPECT_TRUE(watched->WaitToFinish(kWaitMs));
    // The follower ends once the --pid process has, without an error.
    EXPECT_TRUE(follower->WaitToFinish(kWaitMs));
    ASSERT_TRUE(follower->ExitCode().has_value());
    EXPECT_EQ(follower->ExitCode().value(), 0);
}

TEST_F(BuiltinCommandsTest, TailFollowMultipleFilesPrintsHeadersOnSwitch) {
    WriteFile("/h1", "1\n");
    WriteFile("/h2", "2\n");
    StartProcessOptions options;
    options.stdOut = streams->OpenFile("/tailout", kFileOpenWriteCreateTruncate, kFileCreateMode);
    options.stdErr = streams->OpenFile("/tailerr", kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_NE(options.stdOut, nullptr);
    ASSERT_NE(options.stdErr, nullptr);
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/tail",
        {"-s", "0.01", "-f", "/h1", "/h2"}, "/", options);
    ASSERT_NE(process, nullptr);

    std::string content;
    ASSERT_TRUE(WaitEndsWith(*streams, "/tailout",
        "\n==> /h2 <==\n2\n", content)) << content;

    auto appender = root->OpenFile("/h1", kFileOpenWriteCreateAppend, kFileCreateMode);
    ASSERT_NE(appender, nullptr);
    ASSERT_EQ(appender->Write("more\n", 5), 5);
    appender.reset();
    // Only a file that writes something gets the switch header.
    ASSERT_TRUE(WaitEndsWith(*streams, "/tailout", "\n==> /h1 <==\nmore\n", content)) << content;

    process->TriggerStop();
    EXPECT_TRUE(process->WaitToFinish(kWaitMs));
    ASSERT_TRUE(process->ExitCode().has_value());
    EXPECT_EQ(process->ExitCode().value(), 143);
}

TEST_F(BuiltinCommandsTest, TailFollowStartsWithTheLastFileAsTheLastPrinted) {
    // GNU's tail_forever takes the last FILE as the one last printed, even
    // when the initial pass printed nothing of it: data appended to it then
    // comes with no header of its own.
    WriteFile("/h1", "1\n");
    WriteFile("/h2", "2\n");
    StartProcessOptions options;
    options.stdOut = streams->OpenFile("/tailout", kFileOpenWriteCreateTruncate, kFileCreateMode);
    options.stdErr = streams->OpenFile("/tailerr", kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_NE(options.stdOut, nullptr);
    ASSERT_NE(options.stdErr, nullptr);
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/tail",
        {"-s", "0.01", "-n0", "-f", "/h1", "/h2"}, "/", options);
    ASSERT_NE(process, nullptr);

    std::string content;
    ASSERT_TRUE(WaitEndsWith(*streams, "/tailout", "==> /h1 <==\n\n==> /h2 <==\n", content)) << content;

    auto appender = root->OpenFile("/h2", kFileOpenWriteCreateAppend, kFileCreateMode);
    ASSERT_NE(appender, nullptr);
    ASSERT_EQ(appender->Write("more\n", 5), 5);
    appender.reset();
    ASSERT_TRUE(WaitEndsWith(*streams, "/tailout", "more\n", content)) << content;
    EXPECT_EQ(content, "==> /h1 <==\n\n==> /h2 <==\nmore\n");

    process->TriggerStop();
    EXPECT_TRUE(process->WaitToFinish(kWaitMs));
    ASSERT_TRUE(process->ExitCode().has_value());
    EXPECT_EQ(process->ExitCode().value(), 143);
}

} // namespace Haisos