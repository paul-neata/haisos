#pragma once

#include <stdexcept>
#include <string>

namespace Haisos::Hsh {

// Every error hsh reports: a syntax error, an expansion error, a runtime error.
// |message| is what dash prints after "<shell name>: <line>: ", e.g.
// "Syntax error: Unterminated quoted string" or "x: parameter not set".
class ShellError : public std::runtime_error {
public:
    explicit ShellError(const std::string& message, int line = 0, bool incomplete = false)
        : std::runtime_error(message), m_line(line), m_incomplete(incomplete) {}

    // The line it happened on; 0 when the thrower does not know (expansion
    // errors: the executor adds the line of the command being run).
    int Line() const { return m_line; }

    // True when it happened only because the input ended where more was needed
    // (an unterminated quote, an unclosed $( or `if`, a trailing `&&` ...): an
    // interactive shell then shows PS2, reads another line and tries again.
    bool Incomplete() const { return m_incomplete; }

private:
    int m_line;
    bool m_incomplete;
};

// dash's shape for every diagnostic: "<shellName>: <line>: <message>\n",
// e.g. "hsh: 1: Syntax error: \"fi\" unexpected\n".
inline std::string FormatShellError(const std::string& shellName, int line, const std::string& message) {
    return shellName + ": " + std::to_string(line) + ": " + message + "\n";
}

} // namespace Haisos::Hsh
