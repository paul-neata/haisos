#pragma once

#include <string>
#include <vector>

#include "commands/hsh/HshGlob.h"
#include "commands/hsh/HshVariables.h"
#include "commands/hsh/HshWord.h"

namespace Haisos::Hsh {

struct CommandSubstitutionResult {
    std::string output;   // everything the command wrote to its stdout
    int exitStatus = 0;
};

// What expansion needs from the shell running it. The executor implements it:
// ReadDirectory/Exists with the process's IFileIO (ICurrentProcess::IO() --
// never a filesystem of its own), RunCommandSubstitution by parsing the text
// (ParseProgram) and running it in a subshell with its stdout on a pipe.
class IExpansionHost : public IPathnameSource {
public:
    // Runs |source| -- the text of a $(...) as written, or of a `...` after
    // backquote processing -- whose first line is |line|. Throws ShellError
    // when the shell must stop (a syntax error is reported by the subshell
    // itself and only gives a status).
    virtual CommandSubstitutionResult RunCommandSubstitution(const std::string& source, int line) = 0;
};

// Turns the parser's Words into strings, in POSIX order, as dash does: tilde
// expansion, parameter expansion (every ${...} form), command substitution,
// arithmetic expansion, field splitting by IFS, pathname expansion, quote
// removal.
class Expander {
public:
    Expander(ShellState& state, IExpansionHost& host);

    // Command words and for-loop words: every expansion, field splitting,
    // pathname expansion (unless options.noglob), quote removal. Zero or more
    // fields per word.
    std::vector<std::string> ExpandWords(const std::vector<Word>& words);
    std::vector<std::string> ExpandWord(const Word& word);

    // An assignment's value (x=..., and the operand of ${x:=...}): tilde at the
    // start and after every unquoted ':', parameters, command substitutions,
    // arithmetic, quote removal. No splitting, no globbing.
    std::string ExpandAssignmentValue(const Word& value);

    // A redirection target, a here-string, the case subject: tilde at the
    // start, parameters, command substitutions, arithmetic, quote removal --
    // one string, no splitting, no globbing (dash does neither for a
    // non-interactive shell's redirections: `echo hi > *` writes a file "*").
    std::string ExpandToString(const Word& word);

    // A case pattern: as ExpandToString, but every character that was quoted
    // comes out escaped (EscapeForPattern) so MatchPattern takes it literally,
    // while unquoted * ? [ keep their meaning (`"a"*`, `$x` with x='*', `\*`).
    std::string ExpandPattern(const Word& word);

    // A heredoc body: rawBody as it is when the delimiter was quoted; else the
    // body's parameters, command substitutions and arithmetic expanded (no
    // tilde, no splitting, no globbing).
    std::string ExpandHereDocument(const HereDocument& hereDoc);

private:
    ShellState& m_state;
    IExpansionHost& m_host;
};

} // namespace Haisos::Hsh
