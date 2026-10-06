#include "commands/hsh/HshShell.h"

#include <algorithm>

#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/Filesystem/VirtualPath.h"
#include "src/components/Logger/Logger.h"
#include "src/components/libheaders/ExitCodes.h"
#include "BuiltinCommand.h"
#include "commands/hsh/HshBuiltins.h"
#include "commands/hsh/HshDescriptors.h"
#include "commands/hsh/HshParser.h"
#include "commands/hsh/HshRedirection.h"
#include "commands/hsh/HshSubshell.h"
#include "commands/hsh/HshUnboundedPipe.h"
#include "interfaces/IHaisosOS.h"
#include "interfaces/IProcess.h"

namespace Haisos::Hsh {

namespace {

// How long one WaitToFinish call waits before the stop flag is looked at again.
constexpr uint64_t kWaitSliceMs = 50;
// How long a stopped child is given to finish before the shell stops waiting.
constexpr uint64_t kStopGraceMs = 5000;
// What PATH is when the environment brought none (dash's default).
constexpr const char* kDefaultPath = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
// How many background jobs the shell keeps; past that, finished ones are dropped.
constexpr size_t kMaxJobs = 1024;

} // namespace

std::string OpenFailureReason(IFileIO& io, const std::string& path, bool creating) {
    FileStatus status;
    if (io.Stat(path, status) == 0) {
        return status.type == DirectoryEntryType::Dir ? "Is a directory" : "Permission denied";
    }
    if (!creating) {
        return "No such file";
    }
    FileStatus parent;
    if (io.Stat(VirtualParentOf(io.ResolvePath(path)), parent) != 0 || parent.type != DirectoryEntryType::Dir) {
        return "Directory nonexistent";
    }
    return "Permission denied";
}

Shell::Shell(BuiltinContext& context, Invocation invocation)
    : m_context(context), m_invocation(std::move(invocation)), m_expander(m_state, *this) {
    m_state.variables.ImportFrom(*Process().GetEnvironment());
    // As dash at startup: IFS always (never exported), OPTIND and PPID fresh,
    // the prompts only when the environment did not bring them, PATH defaulted
    // (set, not exported) and PWD exported.
    m_state.variables.Set("IFS", " \t\n");
    m_state.variables.Set("OPTIND", "1");
    m_state.variables.Set("PPID", std::to_string(Process().GetParentPid()));
    if (!m_state.variables.IsSet("PS1")) m_state.variables.Set("PS1", "$ ");
    if (!m_state.variables.IsSet("PS2")) m_state.variables.Set("PS2", "> ");
    if (!m_state.variables.IsSet("PS4")) m_state.variables.Set("PS4", "+ ");
    if (!m_state.variables.IsSet("PATH")) m_state.variables.Set("PATH", kDefaultPath);
    m_state.variables.Set("PWD", IO().GetCurrentDirectory());
    m_state.variables.Export("PWD");

    m_state.arg0 = m_invocation.arg0;
    m_state.positional = std::move(m_invocation.positional);
    m_state.options = m_invocation.options;
    m_state.shellPid = Process().GetPid();
}

int Shell::Run() {
    // A stop can surface from anywhere output is written, an error path
    // included; it always ends the shell the same way.
    try {
        std::string source;
        switch (m_invocation.source) {
        case Invocation::Source::CommandString:
            source = m_invocation.commandString;
            break;
        case Invocation::Source::ScriptFile: {
            auto file = IO().OpenFile(m_invocation.scriptPath, kFileOpenReadOnly);
            if (!file || !ReadWholeDescriptor(*file, source)) {
                WriteErr(FormatShellError("hsh", 0,
                    "cannot open " + m_invocation.scriptPath + ": " + OpenFailureReason(IO(), m_invocation.scriptPath, false)));
                return 2;
            }
            break;
        }
        case Invocation::Source::StandardInput: {
            // Read whole, as dash's block reads amount to for a non-interactive
            // shell: commands reading the same stdin see nothing of the script.
            // hsh--interactive reads an interactive stdin line by line instead.
            auto in = IO().GetDescriptor(IFileIO::kStdIn);
            if (in) {
                char buffer[4096];
                for (;;) {
                    const ssize_t count = in->Read(buffer, sizeof(buffer));
                    if (count == kIOInterrupted) {
                        return kExitCodeStopped;  // the shell was stopped
                    }
                    if (count <= 0) {
                        break;  // the end of the input, or a read error
                    }
                    source.append(buffer, static_cast<size_t>(count));
                }
            }
            break;
        }
        }

        Parser parser(source, {1, false, "end of file"});
        int status = m_state.lastExitStatus;
        for (;;) {
            ParseResult result = parser.ParseNext();
            if (result.status == ParseResult::Status::EndOfInput) {
                break;
            }
            if (result.status == ParseResult::Status::Error) {
                WriteErr(FormatShellError(m_state.arg0, result.errorLine, result.errorMessage));
                return 2;
            }
            try {
                status = ExecuteList(result.commands);
            } catch (const ShellExit& e) {
                return e.status & 0xFF;
            } catch (const ShellError& e) {
                WriteErr(FormatShellError(m_state.arg0, e.Line() ? e.Line() : m_currentLine, e.what()));
                // A non-interactive shell exits on a fatal error; the
                // interactive one (hsh--interactive) goes on.
                return 2;
            }
            if (m_context.StopRequested()) {
                throw ShellStopped{};
            }
        }
        return status;
    } catch (const ShellStopped&) {
        StopChildren();
        // The process records 143, or 141 after StopForBrokenPipe, whatever
        // this returns.
        return kExitCodeStopped;
    }
}

int Shell::ExecuteList(const CommandList& list) {
    int status = m_state.lastExitStatus;
    for (const ListItem& item : list.items) {
        if (item.background) {
            if (!item.andOr.pipelines.empty()) {
                m_currentLine = item.andOr.pipelines.back().line;
            }
            status = StartBackground(item);
            continue;
        }
        status = ExecuteAndOr(item.andOr);
    }
    return status;
}

int Shell::ExecuteAndOr(const AndOrList& andOr) {
    int status = ExecutePipeline(andOr.pipelines[0]);
    for (size_t i = 0; i < andOr.operators.size(); ++i) {
        const bool run = andOr.operators[i] == AndOrOperator::And ? status == 0 : status != 0;
        if (run) {
            status = ExecutePipeline(andOr.pipelines[i + 1]);
        }
    }
    return status;
}

int Shell::ExecutePipeline(const Pipeline& pipeline) {
    int status;
    if (pipeline.commands.size() > 1) {
        m_currentLine = pipeline.line;
        status = ExecutePipelinedStages(pipeline);
    } else {
        status = ExecuteCommand(*pipeline.commands[0]);
    }
    if (pipeline.negated) {
        status = status == 0 ? 1 : 0;
    }
    m_state.lastExitStatus = status;
    return status;
}

// The two ends of one pipe between two stages, held by the shell alone as
// shared_ptrs: a real pipe's ends, out of their slots, or an unbounded pipe's.
struct StagePipe {
    std::shared_ptr<IFileDescriptor> readEnd;
    std::shared_ptr<IFileDescriptor> writeEnd;
};

// Makes the pipe between stage |i| and stage |i + 1|; nullopt when a real
// pipe could not be made ("Pipe call failed"). The pipe is unbounded when its
// reader (stage |i + 1|) runs inside the shell and an earlier or this stage
// does too; otherwise it is a real, bounded pipe -- with the deadlock rule
// explained at ExecutePipelinedStages.
static std::optional<StagePipe> CreateStagePipe(IFileIO& io, bool unbounded) {
    if (unbounded) {
        UnboundedPipeEnds ends = CreateUnboundedPipe();
        return StagePipe{std::move(ends.readEnd), std::move(ends.writeEnd)};
    }
    const auto slots = io.CreatePipe();
    if (!slots) {
        return std::nullopt;
    }
    StagePipe pipe{io.GetDescriptor(slots->first), io.GetDescriptor(slots->second)};
    io.CloseDescriptor(slots->first);
    io.CloseDescriptor(slots->second);
    return pipe;
}

int Shell::ExecutePipelinedStages(const Pipeline& pipeline) {
    const size_t count = pipeline.commands.size();
    // Which stages are programs, to be started at once so they run at the
    // same time; the others run inside the shell, one at a time, each in a
    // subshell.
    std::vector<bool> childStage(count);
    for (size_t i = 0; i < count; ++i) {
        childStage[i] = IsChildStage(*pipeline.commands[i]);
    }

    // The pipes. Why this cannot deadlock: the stages inside the shell run
    // one at a time, in order (pass 2), after every child stage has been
    // started (pass 1). A bounded pipe is used only where its reader is
    // either a running child or the first in-shell stage (which runs at
    // once), so every bounded pipe has a reader that is running or about to
    // run, and the writers that would have to wait for a later in-shell stage
    // write into unbounded pipes, which never block.
    std::vector<StagePipe> pipes(count - 1);
    bool inShellBefore = false;
    for (size_t i = 0; i + 1 < count; ++i) {
        inShellBefore = inShellBefore || !childStage[i];
        auto pipe = CreateStagePipe(IO(), !childStage[i + 1] && inShellBefore);
        if (!pipe) {
            Report("Pipe call failed");  // dash's wording; nothing started
            return 2;
        }
        pipes[i] = std::move(*pipe);
    }

    auto inFor = [&](size_t i) -> std::shared_ptr<IFileDescriptor> {
        return i == 0 ? IO().GetDescriptor(IFileIO::kStdIn) : pipes[i - 1].readEnd;
    };
    auto outFor = [&](size_t i) -> std::shared_ptr<IFileDescriptor> {
        return i + 1 == count ? IO().GetDescriptor(IFileIO::kStdOut) : pipes[i].writeEnd;
    };
    // Once a stage has been started or has run, the shell drops its own
    // references to the ends it handed over, so a reader sees end of file
    // once its writers are gone.
    auto releaseFor = [&](size_t i) {
        if (i > 0) {
            pipes[i - 1].readEnd.reset();
        }
        if (i + 1 < count) {
            pipes[i].writeEnd.reset();
        }
    };

    // Pass 1: start every child stage, in order.
    std::vector<std::shared_ptr<IProcess>> stageChild(count);
    std::vector<int> stageStatus(count, 0);
    for (size_t i = 0; i < count; ++i) {
        if (!childStage[i]) {
            continue;
        }
        stageStatus[i] = RunStage(*pipeline.commands[i], inFor(i), outFor(i), &stageChild[i]);
        releaseFor(i);
    }
    // Pass 2: run every in-shell stage, in order, each in a subshell.
    for (size_t i = 0; i < count; ++i) {
        if (childStage[i]) {
            continue;
        }
        stageStatus[i] = RunStage(*pipeline.commands[i], inFor(i), outFor(i), nullptr);
        releaseFor(i);
    }
    // Pass 3: wait for every child stage, in order.
    for (size_t i = 0; i < count; ++i) {
        if (stageChild[i]) {
            stageStatus[i] = WaitForChild(stageChild[i]);
        }
    }
    // The pipeline's status is the last stage's: its child's exit code, or
    // the in-shell status (a child stage that could not start has the 127/126
    // of its report).
    return stageStatus[count - 1];
}

int Shell::ExecuteCommand(const Command& command) {
    ThrowIfStopRequested();
    if (command.kind == CommandKind::Simple) {
        return ExecuteSimpleCommand(static_cast<const SimpleCommand&>(command));
    }
    m_currentLine = command.line;
    // hsh--control-flow removes each of these.
    switch (command.kind) {
    case CommandKind::BraceGroup:         return NotYet("{ }");
    case CommandKind::Subshell:           return NotYet("( )");
    case CommandKind::If:                 return NotYet("if");
    case CommandKind::While:              return NotYet("while");
    case CommandKind::Until:              return NotYet("until");
    case CommandKind::For:                return NotYet("for");
    case CommandKind::Case:               return NotYet("case");
    case CommandKind::FunctionDefinition: return NotYet("function definitions");
    default:                              return NotYet("compound commands");
    }
}

// How a prefix assignment ended for a variable: what to put back after a
// regular builtin has run (a special builtin's assignments stay).
struct ShellAssignmentRestore {
    std::string name;
    std::optional<std::string> value;
    bool exported = false;
};

int Shell::ExecuteSimpleCommand(const SimpleCommand& command) {
    m_currentLine = command.line;
    const uint64_t substitutions = m_substitutionCount;
    const std::vector<std::string> fields = m_expander.ExpandWords(command.words);

    // The redirections change the shell's own descriptor table for the length
    // of the command and are undone afterwards, unless a builtin (exec with no
    // command) asks to keep them. A failure: the command does not run and its
    // prefix assignments are not made; fatal for a special builtin, as dash.
    RedirectionScope scope(*this);
    if (const std::optional<std::string> error = scope.Apply(command.redirections)) {
        if (!fields.empty()) {
            if (const ShellBuiltin* builtin = FindShellBuiltin(fields[0]); builtin && builtin->special) {
                Fail(*error);
            }
        }
        Report(*error);
        return 2;
    }

    if (fields.empty()) {
        // Assignments alone (x=1): applied to the shell. The command's status
        // is the last command substitution's, if one ran, else 0.
        for (const Assignment& assignment : command.assignments) {
            const std::string value = m_expander.ExpandAssignmentValue(assignment.value);
            if (!m_state.variables.Set(assignment.name, value)) {
                Fail(assignment.name + ": is read only");
            }
        }
        return m_substitutionCount != substitutions ? m_state.lastExitStatus : 0;
    }

    if (const ShellBuiltin* builtin = FindShellBuiltin(fields[0])) {
        std::vector<ShellAssignmentRestore> restore;  // regular builtins only
        for (const Assignment& assignment : command.assignments) {
            const std::string value = m_expander.ExpandAssignmentValue(assignment.value);
            if (!builtin->special) {
                restore.push_back({assignment.name, m_state.variables.Get(assignment.name),
                    m_state.variables.IsExported(assignment.name)});
            }
            if (!m_state.variables.Set(assignment.name, value)) {
                Fail(assignment.name + ": is read only");
            }
        }
        const int status = builtin->run(*this, fields);
        if (m_keepRedirections) {
            // exec with no command: the redirections stay.
            scope.Keep();
            m_keepRedirections = false;
        }
        for (auto it = restore.rbegin(); it != restore.rend(); ++it) {
            if (it->value) {
                m_state.variables.Set(it->name, *it->value);
            } else {
                m_state.variables.Unset(it->name);
            }
            if (it->exported) m_state.variables.Export(it->name);
        }
        return status;
    }

    // Anything else is a child process.
    std::vector<std::pair<std::string, std::string>> assignments;
    for (const Assignment& assignment : command.assignments) {
        if (m_state.variables.IsReadonly(assignment.name)) {
            Fail(assignment.name + ": is read only");
        }
        assignments.emplace_back(assignment.name, m_expander.ExpandAssignmentValue(assignment.value));
    }
    const CommandLookup lookup = LookUpCommand(fields[0]);
    if (lookup.result == CommandLookup::Result::NotFound) {
        Report(fields[0] + ": not found");
        return 127;
    }
    if (lookup.result == CommandLookup::Result::NotRunnable) {
        Report(fields[0] + ": Permission denied");
        return 126;
    }
    std::shared_ptr<IProcess> child = StartChild(lookup.path,
        std::vector<std::string>(fields.begin() + 1, fields.end()), assignments);
    if (!child) {
        // The OS refused it: not a program it can run (/notes.txt, say) or the
        // process limit.
        LogDebug("hsh: could not start '%s' as a process", lookup.path.c_str());
        Report(fields[0] + ": Permission denied");
        return 126;
    }
    if (m_startInsteadOfWait) {
        // A pipeline stage: started, not waited for (RunStage).
        *m_startInsteadOfWait = child;
        return 0;
    }
    return WaitForChild(child);
}

BuiltinContext& Shell::Context() { return m_context; }
ICurrentProcess& Shell::Process() { return m_context.Process(); }
IFileIO& Shell::IO() { return m_context.IO(); }
ShellState& Shell::State() { return m_state; }
Expander& Shell::Expansion() { return m_expander; }
int Shell::CurrentLine() const { return m_currentLine; }

void Shell::WriteOut(const std::string& bytes) { WriteDescriptor(IFileIO::kStdOut, bytes); }
void Shell::WriteErr(const std::string& bytes) { WriteDescriptor(IFileIO::kStdErr, bytes); }

void Shell::WriteDescriptor(int fd, const std::string& bytes) {
    // A shell whose output found a broken pipe is dying quietly: nothing more
    // may go out, as for any program after SIGPIPE.
    if (m_brokenPipe) {
        return;
    }
    auto descriptor = IO().GetDescriptor(fd);
    if (!descriptor) {
        return;
    }
    size_t written = 0;
    while (written < bytes.size()) {
        const ssize_t result = descriptor->Write(bytes.data() + written, bytes.size() - written);
        if (result == kIOBrokenPipe) {
            // The shell ends like a program killed by SIGPIPE. At the top level
            // the process itself records the broken pipe (exit code 141); in a
            // subshell only the subshell dies of it, as a forked dash subshell
            // would, and the shell goes on (the flag and the stop are the
            // top-level output's, not the subshell's).
            if (m_subshellDepth > 0) {
                throw ShellExit{kExitCodeBrokenPipe};
            }
            Process().StopForBrokenPipe();
            m_brokenPipe = true;
            throw ShellStopped{};
        }
        if (result == kIOInterrupted) {
            throw ShellStopped{};
        }
        if (result < 0) {
            return;  // whatever else failed: the bytes are dropped
        }
        written += static_cast<size_t>(result);
    }
}

void Shell::Report(const std::string& message) {
    WriteErr(FormatShellError(m_state.arg0, m_currentLine, message));
}

void Shell::Fail(const std::string& message) {
    throw ShellError(message, m_currentLine);
}

void Shell::ThrowIfStopRequested() {
    if (m_context.StopRequested()) {
        throw ShellStopped{};
    }
}

void Shell::KeepRedirections() {
    m_keepRedirections = true;
}

bool Shell::IsChildStage(const Command& command) const {
    if (command.kind != CommandKind::Simple) {
        return false;
    }
    const SimpleCommand& simple = static_cast<const SimpleCommand&>(command);
    if (simple.words.empty()) {
        return false;
    }
    const std::optional<std::string> name = LiteralText(simple.words[0]);
    if (!name) {
        return false;  // what the word expands to decides at run time
    }
    // A shell builtin runs inside the shell; hsh--control-flow also excludes
    // its functions here.
    return !FindShellBuiltin(*name);
}

int Shell::RunStage(const Command& command, std::shared_ptr<IFileDescriptor> in,
                    std::shared_ptr<IFileDescriptor> out, std::shared_ptr<IProcess>* started) {
    return RunSubshell([&]() -> int {
        PlaceDescriptor(IO(), IFileIO::kStdIn, std::move(in));
        PlaceDescriptor(IO(), IFileIO::kStdOut, std::move(out));
        m_startInsteadOfWait = started;
        int status;
        try {
            status = ExecuteCommand(command);
        } catch (...) {
            m_startInsteadOfWait = nullptr;
            throw;
        }
        m_startInsteadOfWait = nullptr;
        return status;
    });
}

int Shell::RunSubshell(const std::function<int()>& body) {
    try {
        const SubshellScope scope(*this);
        return body() & 0xFF;
    } catch (const ShellExit& e) {
        return e.status & 0xFF;
    } catch (const ShellError& e) {
        // A fatal error is reported as at the top level and ends only the
        // subshell, with status 2.
        WriteErr(FormatShellError(m_state.arg0, e.Line() ? e.Line() : m_currentLine, e.what()));
        return 2;
    }
    // A ShellStopped passes through: it ends the whole shell.
}

bool Shell::InSubshell() const {
    return m_subshellDepth > 0;
}

std::vector<Shell::Job>& Shell::Jobs() {
    return m_jobs;
}

int Shell::StartBackground(const ListItem& item) {
    // Started without waiting, as a pipeline of children when it can be: one
    // and-or list of one pipeline whose every stage is a program.
    if (item.andOr.pipelines.size() == 1) {
        const Pipeline& pipeline = item.andOr.pipelines[0];
        bool allChildren = true;
        for (const CommandPtr& command : pipeline.commands) {
            if (!IsChildStage(*command)) {
                allChildren = false;
                break;
            }
        }
        if (allChildren) {
            return StartBackgroundPipeline(pipeline);
        }
    }
    return StartBackgroundShell(item);
}

int Shell::StartBackgroundPipeline(const Pipeline& pipeline) {
    const size_t count = pipeline.commands.size();
    // Every stage is a child, so every pipe is a real one: its reader is a
    // running child (see ExecutePipelinedStages for the deadlock rule).
    std::vector<StagePipe> pipes(count - 1);
    for (size_t i = 0; i + 1 < count; ++i) {
        auto pipe = CreateStagePipe(IO(), false);
        if (!pipe) {
            Report("Pipe call failed");
            return 2;
        }
        pipes[i] = std::move(*pipe);
    }
    Job job;
    int stageStatus = 0;
    for (size_t i = 0; i < count; ++i) {
        // An asynchronous list's stdin is /dev/null when there is no job
        // control (a NullInputDescriptor), before its own redirections.
        std::shared_ptr<IFileDescriptor> in =
            i == 0 ? std::static_pointer_cast<IFileDescriptor>(NullInputDescriptor::Create())
                   : pipes[i - 1].readEnd;
        std::shared_ptr<IFileDescriptor> out =
            i + 1 == count ? IO().GetDescriptor(IFileIO::kStdOut) : pipes[i].writeEnd;
        std::shared_ptr<IProcess> child;
        stageStatus = RunStage(*pipeline.commands[i], std::move(in), std::move(out), &child);
        if (i > 0) {
            pipes[i - 1].readEnd.reset();
        }
        if (i + 1 < count) {
            pipes[i].writeEnd.reset();
        }
        if (child) {
            job.processes.push_back(child);
            job.pid = child->GetPid();
        }
    }
    if (job.processes.empty()) {
        return stageStatus;  // nothing started (a stage's report said why)
    }
    RecordJob(std::move(job));
    return 0;
}

int Shell::StartBackgroundShell(const ListItem& item) {
    // The and-or list needs the shell itself (a builtin, &&/||, and from
    // hsh--control-flow, compound commands and functions): a child hsh runs
    // its text, as dash forks a subshell for it. It starts at the path this
    // shell was started from (no PATH lookup), with $0 and the positional
    // parameters passed on, the exported variables as its environment (an
    // unexported one does not reach it, where a forked subshell would see it
    // -- a documented exception), the shell's working directory and its
    // stdout/stderr, and the options that are on among e u f x C a as an
    // invocation argument. Its stdin is empty (a NullInputDescriptor: POSIX,
    // an asynchronous list without job control); a redirection in its own
    // text then gives it one, inside the child.
    std::vector<std::string> args;
    // The letters of the options that are on among e u f x C a, in
    // OptionLetters' order; no argument when none is on.
    std::string letters;
    if (m_state.options.nounset) letters += 'u';
    if (m_state.options.allexport) letters += 'a';
    if (m_state.options.noclobber) letters += 'C';
    if (m_state.options.xtrace) letters += 'x';
    if (m_state.options.noglob) letters += 'f';
    if (m_state.options.errexit) letters += 'e';
    if (!letters.empty()) {
        args.push_back("-" + letters);
    }
    args.push_back("-c");
    args.push_back(BackgroundScript(item));
    args.push_back(m_state.arg0);
    args.insert(args.end(), m_state.positional.begin(), m_state.positional.end());

    const std::shared_ptr<IFileDescriptor> keepIn = IO().GetDescriptor(IFileIO::kStdIn);
    PlaceDescriptor(IO(), IFileIO::kStdIn, NullInputDescriptor::Create());
    std::shared_ptr<IProcess> child = StartChild(Process().Path(), args, {});
    PlaceDescriptor(IO(), IFileIO::kStdIn, keepIn);
    if (!child) {
        // dash's message when it cannot fork.
        Report("fork: Resource temporarily unavailable");
        return 2;
    }
    Job job;
    job.processes.push_back(child);
    job.pid = child->GetPid();
    RecordJob(std::move(job));
    return 0;
}

void Shell::RecordJob(Job job) {
    m_state.lastBackgroundPid = job.pid;
    m_jobs.push_back(std::move(job));
    if (m_jobs.size() <= kMaxJobs) {
        return;
    }
    // Past the bound: drop the finished jobs, oldest first; their processes
    // leave m_liveChildren with them.
    for (auto it = m_jobs.begin(); it != m_jobs.end() && m_jobs.size() > kMaxJobs;) {
        const bool finished = std::all_of(it->processes.begin(), it->processes.end(),
            [](const std::shared_ptr<IProcess>& process) { return process->WaitToFinish(0); });
        if (!finished) {
            ++it;
            continue;
        }
        for (const std::shared_ptr<IProcess>& process : it->processes) {
            m_liveChildren.erase(std::remove(m_liveChildren.begin(), m_liveChildren.end(), process),
                m_liveChildren.end());
        }
        it = m_jobs.erase(it);
    }
}

std::string Shell::BackgroundScript(const ListItem& item) const {
    return BackgroundPrelude() + item.sourceText + "\n";
}

std::string Shell::BackgroundPrelude() const {
    return "";
}

Shell::CommandLookup Shell::LookUpCommand(const std::string& name) {
    CommandLookup lookup;
    FileStatus status;
    if (name.find('/') != std::string::npos) {
        if (IO().Stat(name, status) != 0) {
            return lookup;  // NotFound
        }
        if (status.type == DirectoryEntryType::Dir) {
            lookup.result = CommandLookup::Result::NotRunnable;
            return lookup;
        }
        lookup.result = CommandLookup::Result::Found;
        lookup.path = IO().ResolvePath(name);
        return lookup;
    }
    // No slash: searched in PATH; unset PATH finds nothing, as dash.
    const std::optional<std::string> path = m_state.variables.Get("PATH");
    if (!path) {
        return lookup;
    }
    size_t begin = 0;
    for (;;) {
        const size_t colon = path->find(':', begin);
        const std::string entry = path->substr(begin, colon == std::string::npos ? colon : colon - begin);
        // An empty entry is the working directory; no doubled '/' after one.
        const std::string candidate = entry.empty() ? name
            : entry + (entry.back() == '/' ? "" : "/") + name;
        if (IO().Stat(candidate, status) == 0 && status.type != DirectoryEntryType::Dir) {
            lookup.result = CommandLookup::Result::Found;
            lookup.path = IO().ResolvePath(candidate);
            return lookup;
        }
        if (colon == std::string::npos) {
            break;
        }
        begin = colon + 1;
    }
    return lookup;
}

std::shared_ptr<IEnvironment> Shell::ChildEnvironment(
    const std::vector<std::pair<std::string, std::string>>& assignments) {
    std::shared_ptr<IEnvironment> environment = Process().GetEnvironment();  // already a clone
    for (const std::string& name : environment->GetVariableNames()) {
        environment->RemoveVariable(name);
    }
    for (const auto& [name, value] : m_state.variables.ExportedVariables()) {
        environment->SetVariable(name, value);
    }
    for (const auto& [name, value] : assignments) {
        environment->SetVariable(name, value);
    }
    return environment;
}

std::shared_ptr<IProcess> Shell::StartChild(const std::string& path, const std::vector<std::string>& args,
    const std::vector<std::pair<std::string, std::string>>& assignments) {
    // The OS is asked for at each use and released right after the call: the
    // process is the only door out, and nothing here keeps a path around it.
    auto os = Process().OS();
    if (!os) {
        return nullptr;
    }
    StartProcessOptions options;
    // A closed (empty) slot goes to the child as a ClosedDescriptor, never as
    // null: StartProcessOptions reads null as "use the console", which a
    // closed descriptor must not become.
    options.stdIn = IO().GetDescriptor(IFileIO::kStdIn);
    options.stdOut = IO().GetDescriptor(IFileIO::kStdOut);
    options.stdErr = IO().GetDescriptor(IFileIO::kStdErr);
    if (!options.stdIn) options.stdIn = ClosedDescriptor::Create();
    if (!options.stdOut) options.stdOut = ClosedDescriptor::Create();
    if (!options.stdErr) options.stdErr = ClosedDescriptor::Create();
    options.interactive = false;
    std::shared_ptr<IProcess> child = os->StartProcess(
        ChildEnvironment(assignments), path, args, IO().GetCurrentDirectory(), options);
    if (child) {
        m_liveChildren.push_back(child);
    }
    return child;
}

int Shell::WaitForChild(const std::shared_ptr<IProcess>& child) {
    uint64_t waitedAfterStop = 0;
    bool stopPassedOn = false;
    while (!child->WaitToFinish(kWaitSliceMs)) {
        if (m_context.StopRequested()) {
            if (!stopPassedOn) {
                for (const auto& live : m_liveChildren) {
                    live->TriggerStop();
                }
                stopPassedOn = true;
            }
            waitedAfterStop += kWaitSliceMs;
            if (waitedAfterStop > kStopGraceMs) {
                break;  // it did not stop: give up waiting
            }
        }
    }
    m_liveChildren.erase(std::remove(m_liveChildren.begin(), m_liveChildren.end(), child), m_liveChildren.end());
    if (m_context.StopRequested()) {
        throw ShellStopped{};
    }
    return child->ExitCode().value_or(kExitCodeStopped);
}

void Shell::StopChildren() {
    for (const auto& child : m_liveChildren) {
        child->TriggerStop();
    }
    for (const auto& child : m_liveChildren) {
        child->WaitToFinish(kStopGraceMs);
    }
    m_liveChildren.clear();
}

std::vector<DirectoryEntry> Shell::ReadDirectory(const std::string& path) {
    return IO().ReadDirectory(path);
}

bool Shell::Exists(const std::string& path) {
    FileStatus status;
    return IO().Stat(path, status) == 0;
}

CommandSubstitutionResult Shell::RunCommandSubstitution(const std::string& source, int line) {
    ++m_substitutionCount;
    // The parser already checked this text when it parsed the word holding it,
    // so an error here is rare; report it and give the substitution status 2.
    ParseResult parsed = ParseProgram(source, {line, false, "end of file"});
    if (parsed.status == ParseResult::Status::Error) {
        WriteErr(FormatShellError(m_state.arg0, parsed.errorLine, parsed.errorMessage));
        return {"", 2};
    }
    // An unbounded pipe: the reader (this shell, below) runs only after the
    // writer (the subshell), so a bounded one would deadlock past 64 KiB.
    UnboundedPipeEnds pipe = CreateUnboundedPipe();
    const int status = RunSubshell([&]() -> int {
        PlaceDescriptor(IO(), IFileIO::kStdOut, pipe.writeEnd);
        pipe.writeEnd.reset();
        // The scope puts slot 1 back when the subshell ends, which releases
        // the write end unless a background child of the subshell still holds
        // it -- then the read below waits for that child, as dash waits for
        // end of file.
        return ExecuteList(parsed.commands);
    });
    // Read to end of file.
    std::string output;
    char buffer[4096];
    for (;;) {
        const ssize_t count = pipe.readEnd->Read(buffer, sizeof(buffer));
        if (count == kIOInterrupted) {
            throw ShellStopped{};
        }
        if (count <= 0) {
            break;
        }
        output.append(buffer, static_cast<size_t>(count));
    }
    return {output, status};
}

int Shell::NotYet(const std::string& what) {
    Report(what + " is not supported yet");
    return 2;
}

} // namespace Haisos::Hsh
