#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace Haisos {

class IFileIO;

// The test/[ expression evaluator, shared by hsh and the test and [ builtins
// (see commands/hsh/HshBuiltinTest.cpp and commands/test/Test.cpp).
enum class TestDialect { Dash, Gnu };

// Evaluates a test/[ expression over args[begin, end) -- operands only: the
// caller keeps its own name out (and, for [, its own trailing "]").
//
//   Dash: hsh's evaluator, dash's test -- the one the shell runs.
//   Gnu:  the test and [ programs, GNU coreutils 9.4's src/test.c.
//
// Syntax errors are reported once through |report|, as the bare message
// without any "<command>: " prefix (the caller adds its own, as hsh's shell
// does), and give exit status 2. Otherwise 0: true, 1: false. |command| is
// the command being run ("test" or "["), for callers that report with a
// prefix. |io| is the process's file access: every file test reads through it.
int EvaluateTestExpression(TestDialect dialect, const std::string& command,
    const std::vector<std::string>& args, size_t begin, size_t end,
    IFileIO& io, const std::function<void(const std::string&)>& report);

} // namespace Haisos