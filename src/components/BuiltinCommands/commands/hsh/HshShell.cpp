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
#include "commands/hsh/HshPattern.h"
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

// Whether a $(...) sits anywhere in |parts| (DoubleQuoted contents, parameter
// operands and arithmetic expressions included). A word holding one expands
// at run time.
bool PartsHoldCommandSubstitution(const std::vector<WordPart>& parts) {
    for (const WordPart& part : parts) {
        if (part.kind == WordPartKind::CommandSubstitution ||
            PartsHoldCommandSubstitution(part.parts)) {
            return true;
        }
    }
    return false;
}

} // namespace

std::atomic<long> Shell::s_liveChildCount{0};

// static
long Shell::LiveChildCountForTest() {
    return s_liveChildCount.load();
}

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
        // -n (noexec): dash checks it at every evaltree, so a `set -n` takes
        // effect mid-list; an interactive shell is unaffected.
        if (m_state.options.noexec && !m_state.options.interactive) {
            continue;
        }
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
    int status = m_state.lastExitStatus;
    // dash re-checks -n (noexec) for every branch of an &&/|| chain too
    // (evaltree recurses into them).
    const auto noexec = [this] {
        return m_state.options.noexec && !m_state.options.interactive;
    };
    // For -e (errexit): every pipeline but the last, and a negated one, is a
    // tested context -- its failure never exits the shell. Only the failure
    // of the list's last pipeline (when it actually ran, was not negated and
    // is not inside a tested context) exits, as dash.
    const auto runPipeline = [this, &andOr](size_t i) -> int {
        const Pipeline& pipeline = andOr.pipelines[i];
        if (i + 1 < andOr.pipelines.size() || pipeline.negated) {
            const TestedContext tested(*this);
            return ExecutePipeline(pipeline);
        }
        return ExecutePipeline(pipeline);
    };
    size_t lastRan = andOr.pipelines.size();  // none ran yet
    if (!noexec()) {
        status = runPipeline(0);
        lastRan = 0;
    }
    for (size_t i = 0; i < andOr.operators.size(); ++i) {
        const bool run = andOr.operators[i] == AndOrOperator::And ? status == 0 : status != 0;
        if (run && !noexec()) {
            status = runPipeline(i + 1);
            lastRan = i + 1;
        }
    }
    if (lastRan + 1 == andOr.pipelines.size() && status != 0 && m_state.options.errexit &&
        !m_state.options.interactive && !andOr.pipelines[lastRan].negated && m_errexitSuppressed == 0) {
        throw ShellExit{status};
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
    if (command.kind == CommandKind::FunctionDefinition) {
        // Only entered in the table; the body (with its redirections) runs on
        // each call. A later definition of the name replaces the earlier one.
        const auto& definition = static_cast<const FunctionDefinition&>(command);
        auto kept = std::make_shared<FunctionDefinition>();
        kept->line = definition.line;
        kept->name = definition.name;
        kept->body = definition.body;
        kept->sourceText = definition.sourceText;
        m_functions[definition.name] = std::move(kept);
        return 0;
    }
    // A compound command's redirections apply to the whole of it
    // (`while read l; do ...; done < f`), undone when it ends; a failure is
    // an ordinary error (status 2), compound commands not being special
    // builtins. "Bad fd number" out of Apply stays fatal, as dash.
    RedirectionScope scope(*this);
    if (const std::optional<std::string> error = scope.Apply(command.redirections)) {
        Report(*error);
        return 2;
    }
    switch (command.kind) {
    case CommandKind::BraceGroup:
        return ExecuteList(static_cast<const BraceGroup&>(command).body);
    case CommandKind::Subshell:
        return RunSubshell([&] { return ExecuteList(static_cast<const Subshell&>(command).body); });
    case CommandKind::If:
        return ExecuteIf(static_cast<const IfCommand&>(command));
    case CommandKind::While:
    case CommandKind::Until:
        return ExecuteLoop(static_cast<const LoopCommand&>(command));
    case CommandKind::For:
        return ExecuteFor(static_cast<const ForCommand&>(command));
    case CommandKind::Case:
        return ExecuteCase(static_cast<const CaseCommand&>(command));
    default:
        return 2;  // unreachable: every kind was handled above
    }
}

int Shell::ExecuteIf(const IfCommand& command) {
    for (const IfBranch& branch : command.branches) {
        int condition;
        {
            const TestedContext tested(*this);  // a condition never trips -e
            condition = ExecuteList(branch.condition);
        }
        if (condition == 0) {
            return ExecuteList(branch.body);
        }
    }
    if (command.elseBody) {
        return ExecuteList(*command.elseBody);
    }
    return 0;
}

// Runs a while/until loop or a for loop's iterations over |words|: the body
// once per round until the condition says stop / the words run out, the last
// body's status (0 when it never ran). break/continue (LoopControl) unwind
// here, |levels| loops at a time.
int Shell::ExecuteLoop(const LoopCommand& command) {
    ++m_loopDepth;
    int status = 0;
    try {
        for (;;) {
            // break/continue may come from the condition too (`while break;
            // do ...`): it ends or repeats this loop, as dash, never an outer one.
            bool inBody = false;
            try {
                int condition;
                {
                    const TestedContext tested(*this);
                    condition = ExecuteList(command.condition);
                }
                const bool again = command.kind == CommandKind::While ? condition == 0 : condition != 0;
                if (!again) {
                    break;
                }
                inBody = true;
                status = ExecuteList(command.body);
            } catch (LoopControl& control) {
                if (control.levels > 1) {  // break/continue n: this loop is one of them
                    --control.levels;
                    throw;
                }
                if (inBody) {
                    status = 0;  // the body ended in break/continue, whose status is 0 (dash)
                }
                if (control.isBreak) {
                    break;
                }
                // continue: the condition again.
            }
        }
    } catch (...) {
        --m_loopDepth;
        throw;
    }
    --m_loopDepth;
    return status;
}

int Shell::ExecuteFor(const ForCommand& command) {
    const std::vector<std::string> words =
        command.hasIn ? m_expander.ExpandWords(command.words) : m_state.positional;
    ++m_loopDepth;
    int status = 0;
    try {
        for (const std::string& word : words) {
            // A read-only loop variable is fatal ("r: is read only"), as dash.
            AssignVariable(command.variable, word);
            try {
                status = ExecuteList(command.body);
            } catch (LoopControl& control) {
                if (control.levels > 1) {
                    --control.levels;
                    throw;
                }
                status = 0;  // the body ended in break/continue, whose status is 0 (dash)
                if (control.isBreak) {
                    break;
                }
            }
        }
    } catch (...) {
        --m_loopDepth;
        throw;
    }
    --m_loopDepth;
    return status;
}

int Shell::ExecuteCase(const CaseCommand& command) {
    const std::string subject = m_expander.ExpandToString(command.subject);
    for (const CaseItem& item : command.items) {
        for (const Word& pattern : item.patterns) {
            if (MatchPattern(m_expander.ExpandPattern(pattern), subject)) {
                // The first match runs; later items' patterns stay unexpanded.
                return item.body.items.empty() ? 0 : ExecuteList(item.body);
            }
        }
    }
    return 0;  // no match, as dash
}

int Shell::CallFunction(const FunctionDefinition& function, const std::vector<std::string>& fields) {
    // The call's own positional parameters ($0 does not change), the loop
    // depth from 0: dash counts loops inside a function on their own, so a
    // break never leaves the caller's loop.
    const std::vector<std::string> keepPositional = std::move(m_state.positional);
    m_state.positional.assign(fields.begin() + 1, fields.end());
    const int keepLoopDepth = m_loopDepth;
    m_loopDepth = 0;
    ++m_functionDepth;
    int status = 0;
    const auto restore = [&] {
        --m_functionDepth;
        m_loopDepth = keepLoopDepth;
        m_state.positional = std::move(keepPositional);
    };
    try {
        status = ExecuteCommand(*function.body);
    } catch (const FunctionReturn& ret) {
        status = ret.status;
    } catch (...) {
        restore();
        throw;
    }
    restore();
    return status;
}

bool Shell::HasFunction(const std::string& name) const {
    return m_functions.find(name) != m_functions.end();
}

void Shell::RemoveFunction(const std::string& name) {
    m_functions.erase(name);
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
    // -n (noexec): dash re-checks it at every evaltree -- a `set -n` stops
    // the rest even mid-list, mid-chain and mid-pipeline; an interactive
    // shell is unaffected.
    if (m_state.options.noexec && !m_state.options.interactive) {
        return m_state.lastExitStatus;
    }

    // The redirections change the shell's own descriptor table for the length
    // of the command and are undone afterwards, unless a builtin (exec with no
    // command) asks to keep them. As dash they apply before the prefix
    // assignments even expand and before the trace: a failure runs none of
    // them, and is fatal for a special builtin. The trace still goes to the
    // stderr in place before them, as dash's (`echo a 2>f` traces to it).
    const std::shared_ptr<IFileDescriptor> traceErr = IO().GetDescriptor(IFileIO::kStdErr);
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

    std::vector<std::pair<std::string, std::string>> assignments;
    for (const Assignment& assignment : command.assignments) {
        assignments.emplace_back(assignment.name, m_expander.ExpandAssignmentValue(assignment.value));
    }

    // dash's trace, after the assignments are made (so a `PS4=X` assignment
    // restyles its own line), to the stderr from before the redirections:
    // PS4 as it is (not expanded, a documented exception), then the
    // assignments as name=value and the fields, joined by single spaces.
    const auto trace = [this, &assignments, &fields, &traceErr] {
        std::string text = m_state.variables.Get("PS4").value_or("");
        bool first = true;
        for (const auto& [name, value] : assignments) {
            text += (first ? "" : " ") + name + "=" + value;
            first = false;
        }
        for (const std::string& field : fields) {
            text += (first ? "" : " ") + field;
            first = false;
        }
        WriteTo(traceErr, text + "\n");
    };

    if (fields.empty()) {
        // Assignments alone (x=1): applied to the shell. The command's status
        // is the last command substitution's, if one ran, else 0.
        for (const auto& [name, value] : assignments) {
            AssignVariable(name, value);
        }
        if (m_state.options.xtrace) {
            trace();
        }
        return m_substitutionCount != substitutions ? m_state.lastExitStatus : 0;
    }

    // POSIX lookup order: special builtin, function, regular builtin, PATH --
    // a function may override cd or test, not exit.
    const ShellBuiltin* builtin = FindShellBuiltin(fields[0]);
    const auto function = m_functions.find(fields[0]);
    // The running definition is held for the call: the body may redefine or
    // `unset -f` its own name, which would free it from m_functions mid-run.
    const CommandPtr held = function != m_functions.end() ? function->second : nullptr;
    if (builtin && builtin->special) {
        // A special builtin's prefix assignments stay (x=1 : sets x).
        for (const auto& [name, value] : assignments) {
            AssignVariable(name, value);
        }
        if (m_state.options.xtrace) {
            trace();
        }
        const int status = builtin->run(*this, fields);
        if (m_keepRedirections) {
            // exec with no command: the redirections stay.
            scope.Keep();
            m_keepRedirections = false;
        }
        return status;
    }
    if (held || builtin) {
        // A regular builtin or a function: the prefix assignments are made for
        // the command only and put back afterwards (x=1 true leaves x unset).
        std::vector<ShellAssignmentRestore> restore;
        for (const auto& [name, value] : assignments) {
            restore.push_back({name, m_state.variables.Get(name),
                m_state.variables.IsExported(name)});
            AssignVariable(name, value);
        }
        if (m_state.options.xtrace) {
            trace();
        }
        const int status = held
            ? CallFunction(static_cast<const FunctionDefinition&>(*held), fields)
            : builtin->run(*this, fields);
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

    // Anything else is a child process. A read-only variable is an error
    // after the assignments have expanded and before the trace, as dash.
    for (const auto& [name, value] : assignments) {
        if (m_state.variables.IsReadonly(name)) {
            Fail(name + ": is read only");
        }
    }
    if (m_state.options.xtrace) {
        trace();
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
    WriteTo(IO().GetDescriptor(fd), bytes);
}

void Shell::WriteTo(const std::shared_ptr<IFileDescriptor>& descriptor, const std::string& bytes) {
    // A shell whose output found a broken pipe is dying quietly: nothing more
    // may go out, as for any program after SIGPIPE.
    if (m_brokenPipe) {
        return;
    }
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

void Shell::AssignVariable(const std::string& name, const std::string& value) {
    if (!m_state.variables.Set(name, value)) {
        Fail(name + ": is read only");
    }
    if (m_state.options.allexport) {
        m_state.variables.Export(name);
    }
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
    // A command substitution anywhere in the words (or the prefix assignments)
    // expands when the command runs. Child stages run in pass 1, before the
    // in-shell stages of pass 2 -- so `: | echo $(cat)` would read a pipe
    // nothing has been written to yet: such a stage runs in the shell too.
    for (const Word& word : simple.words) {
        if (PartsHoldCommandSubstitution(word.parts)) {
            return false;
        }
    }
    for (const Assignment& assignment : simple.assignments) {
        if (PartsHoldCommandSubstitution(assignment.value.parts)) {
            return false;
        }
    }
    const std::optional<std::string> name = LiteralText(simple.words[0]);
    if (!name) {
        return false;  // what the word expands to decides at run time
    }
    // A shell builtin or a function runs inside the shell.
    return !FindShellBuiltin(*name) && !HasFunction(*name);
}

int Shell::RunStage(const Command& command, std::shared_ptr<IFileDescriptor> in,
                    std::shared_ptr<IFileDescriptor> out, std::shared_ptr<IProcess>* started) {
    return RunSubshell([&]() -> int {
        PlaceDescriptor(IO(), IFileIO::kStdIn, std::move(in));
        PlaceDescriptor(IO(), IFileIO::kStdOut, std::move(out));
        // Kept and restored, not simply nulled: a command substitution in the
        // command's own expansion runs a pipeline of its own, which uses this
        // very slot.
        std::shared_ptr<IProcess>* const keep = m_startInsteadOfWait;
        m_startInsteadOfWait = started;
        int status;
        try {
            status = ExecuteCommand(command);
        } catch (...) {
            m_startInsteadOfWait = keep;
            throw;
        }
        m_startInsteadOfWait = keep;
        return status;
    });
}

int Shell::RunSubshell(const std::function<int()>& body) {
    try {
        const SubshellScope scope(*this);
        return body() & 0xFF;
    } catch (const ShellExit& e) {
        // `exit`, or errexit inside the subshell: its status is the subshell
        // command's (which may then trip errexit outside).
        return e.status & 0xFF;
    } catch (const LoopControl&) {
        // A break/continue crossing the subshell boundary (the loop it meant
        // is outside: `for ...; do (break); ...; done`) just ends the subshell,
        // as a forked dash subshell could not reach the outer loop.
        return 0;
    } catch (const FunctionReturn& ret) {
        return ret.status & 0xFF;
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
            ForgetLiveChild(process);
        }
        it = m_jobs.erase(it);
    }
}

void Shell::ForgetLiveChild(const std::shared_ptr<IProcess>& child) {
    const size_t before = m_liveChildren.size();
    m_liveChildren.erase(std::remove(m_liveChildren.begin(), m_liveChildren.end(), child),
        m_liveChildren.end());
    s_liveChildCount -= static_cast<long>(before - m_liveChildren.size());
}

std::string Shell::BackgroundScript(const ListItem& item) const {
    return BackgroundPrelude() + item.sourceText + "\n";
}

std::string Shell::BackgroundPrelude() const {
    // Every function defined, as written: the background child hsh then has
    // them (`f() { echo in-bg; }; f & wait`), as a forked dash subshell would.
    std::string prelude;
    for (const auto& [name, definition] : m_functions) {
        prelude += static_cast<const FunctionDefinition&>(*definition).sourceText;
        prelude += '\n';
    }
    return prelude;
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
        ++s_liveChildCount;
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
    --s_liveChildCount;
    if (m_context.StopRequested()) {
        throw ShellStopped{};
    }
    return child->ExitCode().value_or(kExitCodeStopped);
}

void Shell::StopChildren() {
    // Each child's pid and path are captured before it is signalled: the
    // child may be gone when a wait times out, and the name must still be
    // there for the report.
    struct StoppingChild {
        std::shared_ptr<IProcess> process;
        uint64_t pid = 0;
        std::string path;
    };
    std::vector<StoppingChild> children;
    for (const auto& child : m_liveChildren) {
        children.push_back({child, child->GetPid(), child->Path()});
        child->TriggerStop();
    }
    for (const StoppingChild& stopping : children) {
        if (!stopping.process->WaitToFinish(kStopGraceMs)) {
            LogWarning("hsh: child (pid %llu, %s) did not stop within %llu ms",
                static_cast<unsigned long long>(stopping.pid), stopping.path.c_str(),
                static_cast<unsigned long long>(kStopGraceMs));
        }
    }
    s_liveChildCount -= static_cast<long>(m_liveChildren.size());
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
    // The substitution may sit in the words of a pipeline stage that is
    // started without waiting (RunStage): its own commands run and are waited
    // for as usual, so the no-wait mode is off for its length.
    std::shared_ptr<IProcess>* const keepStartInsteadOfWait = m_startInsteadOfWait;
    m_startInsteadOfWait = nullptr;
    int status;
    try {
        status = RunSubshell([&]() -> int {
            PlaceDescriptor(IO(), IFileIO::kStdOut, pipe.writeEnd);
            pipe.writeEnd.reset();
            // The scope puts slot 1 back when the subshell ends, which releases
            // the write end unless a background child of the subshell still
            // holds it -- then the read below waits for that child, as dash
            // waits for end of file.
            return ExecuteList(parsed.commands);
        });
    } catch (...) {
        m_startInsteadOfWait = keepStartInsteadOfWait;
        throw;
    }
    m_startInsteadOfWait = keepStartInsteadOfWait;
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

} // namespace Haisos::Hsh
