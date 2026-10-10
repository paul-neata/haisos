#pragma once
#include <stdexcept>
#include <string>
#include <vector>

namespace Haisos::Awk {

// How every diagnostic names the command.
inline constexpr const char* kAwkName = "awk";
// How gawk names the program operand's text in messages.
inline constexpr const char* kCommandLineSourceName = "cmd. line";

// One piece of program text: the program operand ("cmd. line"), or one -f
// file, named as given on the command line ("-" for standard input).
struct AwkSource {
    std::string name;
    std::string text;
};

struct AwkWarning {
    std::string sourceName;
    int line = 0;
    std::string message;   // "escape sequence `\q' treated as plain `q'"
};

// "awk: <sourceName>:<line>: " -- the start of every diagnostic tied to a
// place in the program ("awk: cmd. line:1: ", "awk: prog.awk:3: ").
std::string AwkLocationPrefix(const std::string& sourceName, int line);

// A syntax error: gawk's two-line report (see FormatAwkSyntaxError).
class AwkSyntaxError : public std::runtime_error {
public:
    AwkSyntaxError(const std::string& message, std::string sourceName, int line,
                   std::string lineText, size_t column);
    const std::string& SourceName() const { return m_sourceName; }
    int Line() const { return m_line; }
    const std::string& LineText() const { return m_lineText; }  // without its newline
    size_t Column() const { return m_column; }  // byte offset of the caret in LineText()

private:
    std::string m_sourceName;
    int m_line;
    std::string m_lineText;
    size_t m_column;
};

// gawk's syntax-error report, byte for byte:
//   <prefix><lineText>\n
//   <prefix><caret padding>^ <message>\n
// <prefix> is AwkLocationPrefix(sourceName, line); the caret padding has one
// byte per byte of lineText before Column(): a tab stays a tab, anything else
// is a space.
std::string FormatAwkSyntaxError(const AwkSyntaxError& error);
// "<prefix>warning: <message>\n"
std::string FormatAwkWarning(const AwkWarning& warning);
// "<prefix>error: <message>\n" -- gawk's non-syntax parse errors.
std::string FormatAwkError(const std::string& sourceName, int line, const std::string& message);

} // namespace Haisos::Awk