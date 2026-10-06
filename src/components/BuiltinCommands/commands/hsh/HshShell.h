#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
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

class Shell;
int BuiltinDot(Shell& shell, const std::vector<std::string>& args);

// `exit`, `return` at the top level, and the end of a subshell: unwinds to
// Shell::Run (or to the subshell boundary) with the status.
struct ShellExit { int status; };

// The shell was asked to stop (TriggerStop), or broke a pipe with its own
// output: unwinds to Shell::Run past everything, subshell boundaries included.
struct ShellStopped {};

// break n / continue n: unwinds to the loops (levels counts them, one loop
// exiting per level).
struct LoopControl { bool isBreak; int levels; };
// return n: unwinds to the function call or the dot script running.
struct FunctionReturn { int status; };

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

    // --- Running commands ---
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
    // Every variable assignment the shell makes goes through here: Set, a
    // read-only variable being fatal (Fail("<name>: is read only")), then
    // Export when allexport (-a) is on.
    void AssignVariable(const std::string& name, const std::string& value);
    // Throws ShellStopped once the shell has been asked to stop.
    void ThrowIfStopRequested();
    // Called by `exec` with no command: the simple command running it keeps
    // its redirections instead of restoring the descriptor table afterwards.
    void KeepRedirections();

    // --- Functions, loops, dot scripts ---
    int LoopDepth() const { return m_loopDepth; }        // loops running in the current function (or top level)
    int FunctionDepth() const { return m_functionDepth; }
    int DotDepth() const { return m_dotDepth; }
    bool HasFunction(const std::string& name) const;
    void RemoveFunction(const std::string& name);

    // For tests: how many children every live shell has started and has
    // neither waited for nor dropped (the size of all m_liveChildren).
    static long LiveChildCountForTest();

    // --- Pipelines, subshells, background jobs ---
    // Whether |command|, as a pipeline stage, is started at once as a child: a
    // SimpleCommand with no $(...) anywhere in its words (which would expand
    // before pass 2 ran the earlier stages -- a pipe stage could deadlock on
    // it), at least one word, the first word plain literal text (LiteralText)
    // naming neither a shell builtin nor a function. Every other stage runs
    // inside the shell, in a subshell.
    bool IsChildStage(const Command& command) const;
    // Runs |command| with its standard input and output replaced by |in| and
    // |out| -- in a subshell (RunSubshell), slots 0 and 1 set with
    // PlaceDescriptor -- and either waits (returns its status) or, when
    // |started| is given and the command turns out to be a program, starts it
    // without waiting: *started is the child and the status is 0.
    int RunStage(const Command& command, std::shared_ptr<IFileDescriptor> in,
                 std::shared_ptr<IFileDescriptor> out, std::shared_ptr<IProcess>* started);
    // Runs |body| as a subshell: inside a SubshellScope, with `exit`
    // (ShellExit) and fatal errors (ShellError: reported as at the top level,
    // status 2) ending only the subshell. ShellStopped passes through.
    // Returns the subshell's status, & 0xFF.
    int RunSubshell(const std::function<int()>& body);
    bool InSubshell() const;  // a SubshellScope is live (the depth is counted)

    // One background job: every process the background list started, and the
    // pid $! shows -- the last stage's.
    struct Job {
        std::vector<std::shared_ptr<IProcess>> processes;
        uint64_t pid = 0;
    };
    // The jobs started with `&` and not yet waited for, oldest first (bounded:
    // past 1024 the finished ones are dropped, oldest first). What `wait`
    // waits on.
    std::vector<Job>& Jobs();

    // The script a background child hsh runs for |item|: BackgroundPrelude(),
    // then item.sourceText, then "\n".
    std::string BackgroundScript(const ListItem& item) const;
    // What the child must know of this shell beyond its environment: the
    // sourceText of every function defined, each followed by "\n", so the
    // child has the functions (the options that are on among e u f x C a
    // reach it as an invocation argument, the exported variables as its
    // environment).
    std::string BackgroundPrelude() const;

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
    // The compound kinds of ExecuteCommand.
    int ExecuteIf(const IfCommand& command);
    int ExecuteLoop(const LoopCommand& command);
    int ExecuteFor(const ForCommand& command);
    int ExecuteCase(const CaseCommand& command);
    // A function call: its own positional parameters (fields[1..], $0 stays),
    // loop depth from 0 (a break inside a function does not reach the caller's
    // loop), FunctionReturn becoming the status. The body's redirections
    // apply on every call (ExecuteCommand does them).
    int CallFunction(const FunctionDefinition& function, const std::vector<std::string>& fields);

    // A tested context for -e (errexit): an if/elif/while/until condition, and
    // in ExecuteAndOr every pipeline but the last of the list plus any negated
    // pipeline. A function called inside one inherits it.
    struct TestedContext {
        explicit TestedContext(Shell& shell) : m_shell(shell) { ++shell.m_errexitSuppressed; }
        ~TestedContext() { --m_shell.m_errexitSuppressed; }
        TestedContext(const TestedContext&) = delete;
        TestedContext& operator=(const TestedContext&) = delete;
        Shell& m_shell;
    };

    void WriteDescriptor(int fd, const std::string& bytes);
    // WriteDescriptor's work on a descriptor already in hand (the trace's
    // stderr from before a command's redirections); null writes nothing.
    void WriteTo(const std::shared_ptr<IFileDescriptor>& descriptor, const std::string& bytes);

    // A `&` list item: as a nowait pipeline when its and-or list is one
    // pipeline of child stages only, else as a child hsh running its text.
    int StartBackground(const ListItem& item);
    int StartBackgroundPipeline(const Pipeline& pipeline);
    int StartBackgroundShell(const ListItem& item);
    // Adds a job (and sets $! to its pid), dropping finished jobs oldest
    // first once the list is past its bound.
    void RecordJob(Job job);
    // ExecutePipeline for two or more commands.
    int ExecutePipelinedStages(const Pipeline& pipeline);
    // Removes |child| from m_liveChildren (and the test count), when present.
    void ForgetLiveChild(const std::shared_ptr<IProcess>& child);

    friend class SubshellScope;  // it saves m_state, m_jobs, m_functions and the depths
    friend int BuiltinDot(Shell& shell, const std::vector<std::string>& args);  // counts m_dotDepth

    BuiltinContext& m_context;
    Invocation m_invocation;
    ShellState m_state;
    Expander m_expander;
    int m_currentLine = 0;
    uint64_t m_substitutionCount = 0;         // RunCommandSubstitution calls, for bare assignments
    std::vector<std::shared_ptr<IProcess>> m_liveChildren;  // started, not yet waited for
    int m_subshellDepth = 0;                  // counted by SubshellScope
    bool m_brokenPipe = false;
    bool m_keepRedirections = false;          // set by exec with no command
    std::vector<Job> m_jobs;                  // background jobs, oldest first
    // Set for a pipeline stage that may start a child without waiting for it
    // (RunStage); null normally.
    std::shared_ptr<IProcess>* m_startInsteadOfWait = nullptr;

    std::map<std::string, CommandPtr> m_functions;  // name -> the FunctionDefinition node
    int m_loopDepth = 0;            // loops running in the current function (or top level)
    int m_functionDepth = 0;        // function calls running
    int m_dotDepth = 0;             // dot scripts running
    int m_errexitSuppressed = 0;    // > 0 inside a tested context (see TestedContext)

    static std::atomic<long> s_liveChildCount;  // LiveChildCountForTest(), tests only
};

} // namespace Haisos::Hsh
