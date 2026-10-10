#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "commands/jq/JqAst.h"

namespace Haisos::Jq {

// One compile error: |message| is what follows "jq: error: " (jq's syntax
// error with its "unexpected X[, expecting Y] (Unix shell quoting issues?)"
// wording, one of jq's notes, or one of its bad-escape messages), and
// |offset| is where jq places it: the offending token's begin, the begin of
// the last token before an end of input, the 'if'/'try'/FORMAT-key an
// unterminated-note is about -- or SIZE_MAX, which jq prints without any
// location (see FormatCompileError).
struct CompileError {
    std::string message;
    size_t offset = 0;
};

// A parsed program: |root| is null when |errors| is not empty. Only the
// first syntax error is reported (jq can find several; that is a documented
// exception), followed where jq adds one by its unterminated-if/try and
// object-key notes.
struct ParseResult {
    std::unique_ptr<Node> root;
    std::vector<CompileError> errors;
};

// Turns |program| into its syntax tree, or into jq 1.7.1's compile errors.
ParseResult ParseProgram(std::string_view program);

// One error as jq prints it:
//   "jq: error: <message> at <top-level>, line <L>:\n<that line><K spaces>\n"
// where L is the 1-based line holding |offset|, the line is printed without
// its '\n', and K = offset - (offset of that line's first byte): jq prints
// that much padding after the line (no caret). An offset of SIZE_MAX is
// printed without any location, as jq prints its top-level-not-given error.
std::string FormatCompileError(std::string_view program, const CompileError& error);

// "jq: 1 compile error\n" or "jq: <n> compile errors\n".
std::string FormatCompileErrorCount(size_t count);

} // namespace Haisos::Jq