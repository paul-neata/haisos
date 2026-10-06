#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "BuiltinCommand.h"
#include "commands/hsh/HshAst.h"
#include "commands/hsh/HshError.h"
#include "commands/hsh/HshExpansion.h"
#include "commands/hsh/HshInvocation.h"
#include "commands/hsh/HshVariables.h"

namespace Haisos::Hsh {

// `exit`, `return` at the top level, and the end of a subshell: unwinds to
// Shell::Run (or to the subshell boundary, hsh--pipelines) with the status.
struct ShellExit { int status; };

// The shell was asked to stop (TriggerStop), or broke a pipe with its own
// output: unwinds to Shell::Run past everything, subshell boundaries included.
struct ShellStopped {};

// Why IFileIO::OpenFile failed, in dash's words, worked out with Stat:
// reading: "No such file" (nothing there), "Is a directory", else
// "Permission denied"; creating: "Is a directory", "Directory nonexistent"
// (the parent of io.ResolvePath(path) is missing or not a directory), else
// "Permission denied".
std::string OpenFailureReason(IFileIO& io, const std::string& path, bool creating);

// The hsh executor: runs one shell -- a -c string, a script file or standard
// input -- inside the builtin's process. Everything it reaches goes through
// the process: files through IO(), other processes through Process().OS(),
// asked for at each use and never stored (ICurrentProcess is the only door).
class Shell : public IExpansionHost {
public:
    Shell(BuiltinContext& context, Invocation invocation);
    ~Shell() override = default;
    Shell(const Shell&) = delete;
    Shell& operator=(const Shell&) = delete;

    // Runs the -c string, the script file or standard input to its end and
    // returns the exit status (0-255).
    int Run();

    // --- Running commands (later tasks fill in the kinds marked "not yet") ---
    int ExecuteList(const CommandList& list);
    int ExecuteAndOr(const AndOrList& andOr);
    int ExecutePipeline(const Pipeline& pipeline);
    int ExecuteCommand(const Command& command);
    int ExecuteSimpleCommand(const SimpleCommand& command);

    // --- What builtins and later tasks use ---
    BuiltinContext& Context();
    ICurrentProcess& Process();
    IFileIO& IO();
    ShellState& State();
    Expander& Expansion();
    int CurrentLine() const;
    // The shell's own output: written to whatever its slot 1 / slot 2 holds
    // now (after redirections), one Write per call, partial writes looped.
    void WriteOut(const std::string& bytes);
    void WriteErr(const std::string& bytes);
    // A diagnostic: FormatShellError(State().arg0, CurrentLine(), message) to WriteErr.
    void Report(const std::string& message);
    // A fatal error: throws ShellError(message, CurrentLine()).
    [[noreturn]] void Fail(const std::string& message);
    // Throws ShellStopped once the shell has been asked to stop.
    void ThrowIfStopRequested();

    // --- Children ---
    struct CommandLookup {
        enum class Result { Found, NotFound, NotRunnable };
        Result result = Result::NotFound;
        std::string path;  // Found: the absolute path to start
    };
    CommandLookup LookUpCommand(const std::string& name);
    // A clone of the process's environment (secrets and LLM identifiers kept)
    // with every variable removed, then every exported shell variable set,
    // then |assignments| set.
    std::shared_ptr<IEnvironment> ChildEnvironment(
        const std::vector<std::pair<std::string, std::string>>& assignments);
    // Starts |path| (absolute) with |args| (argv[1] on) as a child: the
    // environment above, the shell's working directory, and slots 0/1/2 as
    // its stdin/stdout/stderr. Null when there is no OS or it refused.
    std::shared_ptr<IProcess> StartChild(const std::string& path, const std::vector<std::string>& args,
        const std::vector<std::pair<std::string, std::string>>& assignments);
    // Waits for |child| and returns its exit code (kExitCodeStopped if it has
    // none). Passes a stop of the shell on to every live child; throws
    // ShellStopped after |child| has finished if the shell was asked to stop.
    int WaitForChild(const std::shared_ptr<IProcess>& child);
    // TriggerStop on every live child, then waits for each up to kStopGraceMs.
    void StopChildren();

    // --- IPathnameSource / IExpansionHost ---
    std::vector<DirectoryEntry> ReadDirectory(const std::string& path) override;  // IO().ReadDirectory
    bool Exists(const std::string& path) override;                                // IO().Stat(...) == 0
    CommandSubstitutionResult RunCommandSubstitution(const std::string& source, int line) override;

private:
    // A construct this task does not run yet ("redirections is not supported
    // yet", status 2). Each later task removes its uses; hsh--control-flow
    // removes the helper.
    int NotYet(const std::string& what);
    void WriteDescriptor(int fd, const std::string& bytes);

    BuiltinContext& m_context;
    Invocation m_invocation;
    ShellState m_state;
    Expander m_expander;
    int m_currentLine = 0;
    uint64_t m_substitutionCount = 0;         // RunCommandSubstitution calls, for bare assignments
    std::vector<std::shared_ptr<IProcess>> m_liveChildren;  // started, not yet waited for
    int m_subshellDepth = 0;                  // hsh--pipelines; 0 here
    bool m_brokenPipe = false;
};

} // namespace Haisos::Hsh
