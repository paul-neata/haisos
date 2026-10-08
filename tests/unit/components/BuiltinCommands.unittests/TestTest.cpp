#include "BuiltinCommandsFixture.h"

using namespace Haisos;

// The test and [ programs: GNU coreutils' test, over the fixture's files.
// The evaluator itself (BuiltinTestExpression) is tested here through the
// programs only -- its Dash dialect is hsh's, and Hsh.unittests covers it.

TEST_F(BuiltinCommandsTest, TestProgramTruthValues) {
    // No expression is false; a bare string its own non-emptiness.
    EXPECT_EQ(RunCaptured("test", {}).status, 1);
    EXPECT_EQ(RunCaptured("test", {""}).status, 1);
    EXPECT_EQ(RunCaptured("test", {"x"}).status, 0);
    // test takes no options: --help is an ordinary non-empty string, as
    // GNU's test (the [ name shows the help; see BracketProgram below).
    EXPECT_EQ(RunCaptured("test", {"--help"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"-n", "x"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"-z", ""}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"-z", "x"}).status, 1);

    // String comparisons.
    EXPECT_EQ(RunCaptured("test", {"x", "=", "x"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"x", "!=", "x"}).status, 1);
    EXPECT_EQ(RunCaptured("test", {"x", "==", "x"}).status, 0);

    // Integer comparisons, of any length, with -l for a string's length.
    EXPECT_EQ(RunCaptured("test", {"1", "-eq", "1"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"1", "-ne", "1"}).status, 1);
    EXPECT_EQ(RunCaptured("test", {"2", "-gt", "1"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"-5", "-lt", "-3"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"-5", "-ge", "-3"}).status, 1);
    EXPECT_EQ(RunCaptured("test", {"123456789012345678901234567890", "-eq",
        "123456789012345678901234567890"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"00", "-eq", "0"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {" 12 ", "-eq", "12"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"-l", "abc", "-eq", "3"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"3", "-eq", "-l", "abc"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"-l", "abc", "-eq", "-l", "xyz"}).status, 0);

    // !, -a, -o and parentheses.
    EXPECT_EQ(RunCaptured("test", {"!", "x"}).status, 1);
    EXPECT_EQ(RunCaptured("test", {"!", "", "-a", "y"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"!", "!", "x"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"x", "-a", "y"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"", "-a", "x"}).status, 1);
    EXPECT_EQ(RunCaptured("test", {"", "-o", "x"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"x", "-a", "(", "y", ")"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"(", "x", ")", "-a", "y"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"(", "x", "-a", "y", ")"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"(", "x", "-o", "y", ")"}).status, 0);

    // The file tests, on the fixture's files.
    EXPECT_EQ(RunCaptured("test", {"-e", "/notes.txt"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"-e", "/nosuch"}).status, 1);
    EXPECT_EQ(RunCaptured("test", {"-f", "/notes.txt"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"-f", "/docs"}).status, 1);
    EXPECT_EQ(RunCaptured("test", {"-d", "/docs"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"-d", "/notes.txt"}).status, 1);
    EXPECT_EQ(RunCaptured("test", {"-s", "/notes.txt"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"-s", "/nosuch"}).status, 1);
    // No permissions or users: these only test that the file is there.
    for (const char* op : {"-r", "-w", "-x", "-O", "-G"}) {
        EXPECT_EQ(RunCaptured("test", {op, "/notes.txt"}).status, 0) << op;
        EXPECT_EQ(RunCaptured("test", {op, "/nosuch"}).status, 1) << op;
    }
    // No links, block devices, fifos, sockets or set-id bits in Haisos.
    for (const char* op : {"-h", "-L", "-b", "-p", "-S", "-u", "-g", "-k"}) {
        EXPECT_EQ(RunCaptured("test", {op, "/notes.txt"}).status, 1) << op;
    }
    // -ef: the same file, by resolved path.
    EXPECT_EQ(RunCaptured("test", {"/docs/a.md", "-ef", "/docs/a.md"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"/docs/a.md", "-ef", "/docs/sub/b.md"}).status, 1);
    EXPECT_EQ(RunCaptured("test", {"/docs/a.md", "-ef", "/nosuch"}).status, 1);
    // -nt/-ot: a file that is not there is never newer or older.
    EXPECT_EQ(RunCaptured("test", {"/notes.txt", "-nt", "/nosuch"}).status, 0);
    EXPECT_EQ(RunCaptured("test", {"/nosuch", "-nt", "/notes.txt"}).status, 1);
    EXPECT_EQ(RunCaptured("test", {"/notes.txt", "-ot", "/nosuch"}).status, 1);
    EXPECT_EQ(RunCaptured("test", {"/nosuch", "-ot", "/notes.txt"}).status, 0);
    // -t: nothing a captured run holds is a terminal; a number too large
    // for a descriptor is just false.
    EXPECT_EQ(RunCaptured("test", {"-t", "1"}).status, 1);
    EXPECT_EQ(RunCaptured("test", {"-t", "999999"}).status, 1);
}

TEST_F(BuiltinCommandsTest, TestProgramSyntaxErrors) {
    // Every syntax error is one line on stderr, named after the command,
    // with status 2 -- the words of the messages GNU's test prints.
    const auto run = [this](const std::vector<std::string>& args) {
        return RunCaptured("test", args);
    };
    Captured captured = run({"x", "y"});
    EXPECT_EQ(captured.err, "test: missing argument after 'y'\n");
    captured = run({"x", "="});
    EXPECT_EQ(captured.err, "test: missing argument after '='\n");
    captured = run({"-q", "x"});
    EXPECT_EQ(captured.err, "test: '-q': unary operator expected\n");
    captured = run({"x", "-f", "y"});
    EXPECT_EQ(captured.err, "test: '-f': binary operator expected\n");
    captured = run({"a", "b", "c"});
    EXPECT_EQ(captured.err, "test: 'b': binary operator expected\n");
    captured = run({"1", "-eq", "x"});
    EXPECT_EQ(captured.err, "test: invalid integer 'x'\n");
    captured = run({"x", "-eq", "y"});
    EXPECT_EQ(captured.err, "test: invalid integer 'x'\n");
    captured = run({"-t", "x"});
    EXPECT_EQ(captured.err, "test: invalid integer 'x'\n");
    captured = run({"x", "y", "z", "w"});
    EXPECT_EQ(captured.err, "test: extra argument 'y'\n");
    captured = run({"-l", "a", "-nt", "b"});
    EXPECT_EQ(captured.err, "test: -nt does not accept -l\n");
    captured = run({"a", "-nt", "-l", "b"});
    EXPECT_EQ(captured.err, "test: -nt does not accept -l\n");
    captured = run({"(", ")"});
    EXPECT_EQ(captured.err, "test: missing argument after ')'\n");
    captured = run({"(", ")", "-a", "x"});
    EXPECT_EQ(captured.err, "test: ')' expected\n");
    captured = run({"x", "-a", "(", ")"});
    EXPECT_EQ(captured.err, "test: ')' expected\n");
    captured = run({"(", "a", "b", ")", "-a", "x"});
    EXPECT_EQ(captured.err, "test: missing argument after 'x'\n");
    captured = run({"(", "a", "b", "c", "d", ")"});
    EXPECT_EQ(captured.err, "test: ')' expected, found 'b'\n");
    for (const std::vector<std::string>& args : {std::vector<std::string>{"x", "y"},
            {"x", "="}, {"-q", "x"}, {"1", "-eq", "x"}, {"x", "y", "z", "w"}}) {
        const Captured each = run(args);
        EXPECT_EQ(each.status, 2) << args[0];
        EXPECT_EQ(each.out, "") << args[0];
    }
}

TEST_F(BuiltinCommandsTest, BracketProgram) {
    // [ requires its last argument to be exactly "]".
    Captured captured = RunCaptured("[", {"x"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "[: missing ']'\n");
    EXPECT_EQ(captured.status, 2);
    captured = RunCaptured("[", {"x", "]foo"});
    EXPECT_EQ(captured.err, "[: missing ']'\n");
    EXPECT_EQ(captured.status, 2);

    // "[]" is the empty expression: false. Otherwise the "]" is dropped and
    // the rest evaluated as test's.
    captured = RunCaptured("[", {"]"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 1);
    captured = RunCaptured("[", {"x", "]"});
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("[", {"-f", "/notes.txt", "]"});
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("[", {"-f", "/nosuch", "]"});
    EXPECT_EQ(captured.status, 1);

    // Errors are named after [, not test.
    captured = RunCaptured("[", {"1", "-eq", "x", "]"});
    EXPECT_EQ(captured.err, "[: invalid integer 'x'\n");
    EXPECT_EQ(captured.status, 2);
    captured = RunCaptured("[", {"a", "b", "]"});
    EXPECT_EQ(captured.err, "[: missing argument after 'b'\n");
    EXPECT_EQ(captured.status, 2);
    captured = RunCaptured("[", {"--help", "--help", "]"});
    EXPECT_EQ(captured.err, "[: missing argument after '--help'\n");
    EXPECT_EQ(captured.status, 2);

    // [ alone honors --help and --version, as GNU's does; with a "]" it is
    // an expression again, and test never treats them as options.
    captured = RunCaptured("[", {"--help"});
    EXPECT_EQ(captured.out, BuiltinHelpText(*CreateBracketCommand()));
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("[", {"--version"});
    EXPECT_EQ(captured.out, "[ (HaisosOS builtin) 1.0.0\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("[", {"--help", "]"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("test", {"--version"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, TestProgramReachedThroughPath) {
    // A builtin runs wherever it is placed: test and [ at paths of their
    // own, not only in /bin.
    ASSERT_EQ(root->CreateDirectory("/tools", kDirMode), 0);
    auto configurator = factory->CreateBuiltinConfigurator();
    std::string error;
    ASSERT_TRUE(configurator->AddBuiltinCommand(root, "/tools/test", "test", &error)) << error;
    ASSERT_TRUE(configurator->AddBuiltinCommand(root, "/tools/bracket", "[", &error)) << error;

    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/tools/test",
        {"-f", "/notes.txt"}, "/", StartProcessOptions{});
    ASSERT_NE(process, nullptr);
    EXPECT_TRUE(process->WaitToFinish(kWaitMs));
    EXPECT_EQ(process->ExitCode(), 0);

    process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/tools/bracket",
        {"-d", "/docs", "]"}, "/", StartProcessOptions{});
    ASSERT_NE(process, nullptr);
    EXPECT_TRUE(process->WaitToFinish(kWaitMs));
    EXPECT_EQ(process->ExitCode(), 0);
}