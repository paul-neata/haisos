#include "BuiltinRunProgram.h"

#include "commands/hsh/HshDescriptors.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/libheaders/ExitCodes.h"
#include "interfaces/IHaisosOS.h"
#include "interfaces/IProcess.h"

namespace Haisos {

namespace {

// How long one WaitToFinish call waits before the stop flag is looked at again.
constexpr uint64_t kWaitSliceMs = 50;
// How long a stopped child is given to finish before the caller stops waiting.
constexpr uint64_t kStopGraceMs = 5000;

} // namespace

std::vector<std::string> SearchPathEntries(BuiltinContext& context, const IEnvironment* environment) {
    std::optional<std::string> path;
    if (environment) {
        path = environment->GetVariable("PATH");
    } else if (auto own = context.Process().GetEnvironment()) {
        path = own->GetVariable("PATH");
    }
    if (!path) {
        path = kBuiltinDefaultSearchPath;
    }
    std::vector<std::string> entries;
    size_t begin = 0;
    for (;;) {
        const size_t colon = path->find(':', begin);
        entries.push_back(path->substr(begin, colon == std::string::npos ? colon : colon - begin));
        if (colon == std::string::npos) {
            break;
        }
        begin = colon + 1;
    }
    return entries;
}

std::optional<std::string> FindProgramInPath(BuiltinContext& context, const std::string& name,
                                             const IEnvironment* environment) {
    if (name.empty()) {
        return std::nullopt;
    }
    FileStatus status;
    if (name.find('/') != std::string::npos) {
        // A name with a '/' is taken as it is; a directory is not runnable.
        if (context.IO().Stat(name, status) != 0 || status.type == DirectoryEntryType::Dir) {
            return std::nullopt;
        }
        return context.IO().ResolvePath(name);
    }
    // Otherwise each PATH entry in order, an empty one the working directory;
    // the first existing non-directory wins. hsh's rules exactly.
    for (const std::string& entry : SearchPathEntries(context, environment)) {
        const std::string candidate = entry.empty() ? name
            : entry + (entry.back() == '/' ? "" : "/") + name;
        if (context.IO().Stat(candidate, status) == 0 && status.type != DirectoryEntryType::Dir) {
            return context.IO().ResolvePath(candidate);
        }
    }
    return std::nullopt;
}

std::shared_ptr<IFileDescriptor> OpenEmptyInput(BuiltinContext& context) {
    // The read end of a pipe whose write end is released at once: the child
    // holding it reads nothing and finds the end, exactly as /dev/null's.
    auto slots = context.IO().CreatePipe();
    if (!slots) {
        return nullptr;
    }
    auto input = context.IO().GetDescriptor(slots->first);
    context.IO().CloseDescriptor(slots->second);  // no writer: the end at once
    context.IO().CloseDescriptor(slots->first);
    return input;
}

int RunProgramAndWait(BuiltinContext& context, const std::string& programPath,
                      const std::vector<std::string>& args,
                      const RunProgramOptions& options, bool* started) {
    if (started) {
        *started = true;
    }
    // Whatever the caller has buffered must be out before the child writes to
    // the same descriptor.
    context.Flush();

    StartProcessOptions processOptions;
    processOptions.interactive = false;
    // A closed (empty) slot goes to the child as a ClosedDescriptor, never as
    // null: StartProcessOptions reads null as "use the console".
    processOptions.stdIn = options.stdIn ? options.stdIn : context.IO().GetDescriptor(IFileIO::kStdIn);
    processOptions.stdOut = options.stdOut ? options.stdOut : context.IO().GetDescriptor(IFileIO::kStdOut);
    processOptions.stdErr = options.stdErr ? options.stdErr : context.IO().GetDescriptor(IFileIO::kStdErr);
    if (!processOptions.stdIn) processOptions.stdIn = Hsh::ClosedDescriptor::Create();
    if (!processOptions.stdOut) processOptions.stdOut = Hsh::ClosedDescriptor::Create();
    if (!processOptions.stdErr) processOptions.stdErr = Hsh::ClosedDescriptor::Create();
    std::shared_ptr<IEnvironment> environment =
        options.environment ? options.environment : context.Process().GetEnvironment();
    const std::string workingDirectory =
        options.workingDirectory ? context.IO().ResolvePath(*options.workingDirectory)
                                 : context.IO().GetCurrentDirectory();

    // The OS is asked for at each use and released right after the call: the
    // process is the only door out, and nothing here keeps a path around it.
    auto os = context.Process().OS();
    if (!os) {
        if (started) {
            *started = false;
        }
        return kExitCodeNotStarted;
    }
    std::shared_ptr<IProcess> child =
        os->StartProcess(environment, programPath, args, workingDirectory, processOptions);
    os.reset();
    if (!child) {
        if (started) {
            *started = false;
        }
        return kExitCodeNotStarted;
    }

    // Wait in slices, as Shell::WaitForChild does, so a stop of the caller
    // reaches the child: it is stopped once, then given up to 5 s more.
    uint64_t waitedAfterStop = 0;
    bool stopPassedOn = false;
    bool finished = false;
    while (!finished) {
        finished = child->WaitToFinish(kWaitSliceMs);
        if (finished) {
            break;
        }
        if (context.StopRequested()) {
            if (!stopPassedOn) {
                child->TriggerStop();
                stopPassedOn = true;
            }
            waitedAfterStop += kWaitSliceMs;
            if (waitedAfterStop > kStopGraceMs) {
                break;  // it did not stop: give up waiting
            }
        }
    }
    if (!finished) {
        return kExitCodeStopped;
    }
    return child->ExitCode().value_or(kExitCodeStopped);
}

} // namespace Haisos