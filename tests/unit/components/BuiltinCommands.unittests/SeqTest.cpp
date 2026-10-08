#include "BuiltinCommandsFixture.h"
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace Haisos;

// --- seq ---

TEST_F(BuiltinCommandsTest, SeqCountsUpToLast) {
    Captured captured = RunCaptured("seq", {"3"});
    EXPECT_EQ(captured.out, "1\n2\n3\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("seq", {"2", "5"});
    EXPECT_EQ(captured.out, "2\n3\n4\n5\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("seq", {"1", "2", "10"});
    EXPECT_EQ(captured.out, "1\n3\n5\n7\n9\n");
    EXPECT_EQ(captured.status, 0);

    // Out of range from the start: nothing at all, exit 0.
    captured = RunCaptured("seq", {"10", "1"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SeqNegativeAndDecimalSteps) {
    Captured captured = RunCaptured("seq", {"5", "-2", "0"});
    EXPECT_EQ(captured.out, "5\n3\n1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("seq", {"0.1", "0.3", "1"});
    EXPECT_EQ(captured.out, "0.1\n0.4\n0.7\n1.0\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("seq", {"1", "0.5", "3"});
    EXPECT_EQ(captured.out, "1.0\n1.5\n2.0\n2.5\n3.0\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("seq", {"-0", "2"});
    EXPECT_EQ(captured.out, "-0\n1\n2\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("seq", {"1.0", "3"});
    EXPECT_EQ(captured.out, "1.0\n2.0\n3.0\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("seq", {"1", "3.0"});
    EXPECT_EQ(captured.out, "1\n2\n3\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("seq", {"0.000001", "0.000001", "0.000003"});
    EXPECT_EQ(captured.out, "0.000001\n0.000002\n0.000003\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SeqEqualWidth) {
    Captured captured = RunCaptured("seq", {"-w", "8", "11"});
    EXPECT_EQ(captured.out, "08\n09\n10\n11\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("seq", {"-w", "-3", "1"});
    EXPECT_EQ(captured.out, "-3\n-2\n-1\n00\n01\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("seq", {"-w", "5", "-1", "-2"});
    EXPECT_EQ(captured.out, "05\n04\n03\n02\n01\n00\n-1\n-2\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("seq", {"-w", "0.5", "1.75", "3"});
    EXPECT_EQ(captured.out, "0.50\n2.25\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("seq", {"-w", "1e1", "1e1"});
    EXPECT_EQ(captured.out, "10\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("seq", {"--equal-width", "9", "10"});
    EXPECT_EQ(captured.out, "09\n10\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SeqSeparatorAndFormat) {
    Captured captured = RunCaptured("seq", {"-s,", "1", "4"});
    EXPECT_EQ(captured.out, "1,2,3,4\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("seq", {"-s", ", ", "1", "3"});
    EXPECT_EQ(captured.out, "1, 2, 3\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("seq", {"-s", "", "1", "3"});
    EXPECT_EQ(captured.out, "123\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("seq", {"-f", "%03g", "1", "3"});
    EXPECT_EQ(captured.out, "001\n002\n003\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("seq", {"-f", "x%.2fy", "1", "0.5", "2"});
    EXPECT_EQ(captured.out, "x1.00y\nx1.50y\nx2.00y\n");
    EXPECT_EQ(captured.status, 0);

    // %% pairs around the directive print one % each.
    captured = RunCaptured("seq", {"-f", "%%%g%%", "1", "1"});
    EXPECT_EQ(captured.out, "%1%\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("seq", {"-f", "%e", "1", "2"});
    EXPECT_EQ(captured.out, "1.000000e+00\n2.000000e+00\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SeqBigIntegersAndExponents) {
    // The decimal-string fast path: beyond any fixed-size integer.
    Captured captured = RunCaptured("seq",
        {"99999999999999999999", "99999999999999999999"});
    EXPECT_EQ(captured.out, "99999999999999999999\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("seq", {"1e2", "1e2"});
    EXPECT_EQ(captured.out, "100\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("seq", {"0x10", "0x12"});
    EXPECT_EQ(captured.out, "16\n17\n18\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SeqErrors) {
    Captured captured = RunCaptured("seq", {});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err,
        "seq: missing operand\nTry 'seq --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("seq", {"a"});
    EXPECT_EQ(captured.err,
        "seq: invalid floating point argument: 'a'\n"
        "Try 'seq --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("seq", {"1", "0", "3"});
    EXPECT_EQ(captured.err,
        "seq: invalid Zero increment value: '0'\n"
        "Try 'seq --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("seq", {"nan"});
    EXPECT_EQ(captured.err,
        "seq: invalid 'not-a-number' argument: 'nan'\n"
        "Try 'seq --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("seq", {"1", "2", "3", "4"});
    EXPECT_EQ(captured.err,
        "seq: extra operand '4'\n"
        "Try 'seq --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);

    // A bad -f format ends the command with no Try line.
    captured = RunCaptured("seq", {"-f", "%d", "1", "2"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "seq: format '%d' has unknown %d directive\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("seq", {"-f", "%g %g", "1", "2"});
    EXPECT_EQ(captured.err, "seq: format '%g %g' has too many % directives\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("seq", {"-f", "abc", "1", "2"});
    EXPECT_EQ(captured.err, "seq: format 'abc' has no % directive\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("seq", {"-f", "%", "1"});
    EXPECT_EQ(captured.err, "seq: format '%' ends in %\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("seq", {"-w", "-f", "%g", "1", "2"});
    EXPECT_EQ(captured.err,
        "seq: format string may not be specified when printing equal width strings\n"
        "Try 'seq --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, SeqEndlessStopsOnStop) {
    // seq 1 inf never ends on its own: it must end on TriggerStop().
    StartProcessOptions options;
    options.stdOut = streams->OpenFile("/out", kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_NE(options.stdOut, nullptr);
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(),
        "/bin/seq", {"1", "inf"}, "/", options);
    ASSERT_NE(process, nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    process->TriggerStop();
    EXPECT_TRUE(process->WaitToFinish(kWaitMs));
    ASSERT_TRUE(process->ExitCode().has_value());
    EXPECT_EQ(*process->ExitCode(), 143);
}