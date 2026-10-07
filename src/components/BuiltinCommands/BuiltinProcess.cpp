#include "BuiltinProcess.h"
#include <chrono>
#include "ProcessFileIO.h"
#include "src/components/libheaders/DestroyOffRuntimeThreads.h"
#include "src/components/libheaders/ExitCodes.h"
#include "src/components/Logger/Logger.h"

namespace Haisos {

std::shared_ptr<BuiltinProcess> BuiltinProcess::Create(
    const BuiltinCommandHost& host,
    std::shared_ptr<IEnvironment> environment,
    std::shared_ptr<IBuiltinCommand> command,
    std::vector<std::string> args,
    const std::string& workingDirectory,
    const StartProcessOptions& options)
{
    // The command's own thread can hold the last reference to it -- through
    // an OS it lets go of last (see ProcessFileIO) -- and the destructor joins
    // that thread.
    auto process = std::shared_ptr<BuiltinProcess>(
        new BuiltinProcess(host, std::move(environment), std::move(command), std::move(args), workingDirectory),
        DestroyOffRuntimeThreads<BuiltinProcess>(
            "BuiltinProcess '" + host.programPath + "' pid=" + std::to_string(host.pid)));
    // Descriptors 0/1/2 must be in place before the command's thread starts:
    // BuiltinContext reads them at construction.
    if (!process->m_io->InstallStandardStreams(options.stdIn, options.stdOut, options.stdErr)) {
        LogError("BuiltinProcess '%s' pid=%llu: could not install the standard streams; the command is not run",
            host.programPath.c_str(), static_cast<unsigned long long>(host.pid));
        return nullptr;
    }
    process->Start();
    return process;
}

BuiltinProcess::BuiltinProcess(
    const BuiltinCommandHost& host,
    std::shared_ptr<IEnvironment> environment,
    std::shared_ptr<IBuiltinCommand> command,
    std::vector<std::string> args,
    const std::string& workingDirectory)
    : m_pid(host.pid)
    , m_parentPid(host.parentPid)
    , m_path(host.programPath)
    , m_environment(std::move(environment))
    , m_os(host.os)
    // The same file I/O every other runtime gets: the OS's root plus this
    // process's own working directory.
    , m_io(ProcessFileIO::Create(host.os, workingDirectory))
    , m_command(std::move(command))
    , m_args(std::move(args))
{
}

BuiltinProcess::~BuiltinProcess() {
    LogDebug("BuiltinProcess '%s' pid=%llu: destroying", m_path.c_str(), static_cast<unsigned long long>(m_pid));
    TriggerStop();
    std::lock_guard<std::mutex> joinLock(m_joinMutex);
    if (m_thread.joinable()) {
        // Cannot happen while Create()'s deleter is in place. If it ever does,
        // join() below throws std::system_error, and a throwing destructor is
        // std::terminate: this line is what explains the crash.
        if (m_thread.get_id() == std::this_thread::get_id()) {
            LogError("BuiltinProcess '%s' pid=%llu: destroyed on its own thread, which it cannot join",
                m_path.c_str(), static_cast<unsigned long long>(m_pid));
        }
        m_thread.join();
    }
}

void BuiltinProcess::Start() {
    // Under the join lock, as every use of m_thread is: IsOwnThread() may be
    // asked from the command's thread itself.
    std::lock_guard<std::mutex> joinLock(m_joinMutex);
    m_thread = std::thread(&BuiltinProcess::RunThread, this);
}

bool BuiltinProcess::IsOwnThread() {
    std::lock_guard<std::mutex> joinLock(m_joinMutex);
    return m_thread.get_id() == std::this_thread::get_id();
}

void BuiltinProcess::RunThread() {
    // A runtime thread (see DestroyOffRuntimeThreads.h): each of the command's
    // file operations holds its OS for a moment (see ProcessFileIO), so this
    // thread can be the one to let go of the OS last -- whose destruction then
    // lets go of this very process. Whatever this thread lets go of last is
    // destroyed on the destruction thread, not here. It also names this thread
    // in every log line.
    RuntimeThreadScope runtimeThread("builtin " + m_path + " pid=" + std::to_string(m_pid));
    // The process's stop token on its own thread, so a blocked pipe call
    // notices when the process is asked to stop.
    StopTokenScope stopTokenScope(m_stopToken);
    const std::string name = m_command->Name();
    int status = 1;
    try {
        // In a scope of its own, so its final flush happens before the
        // descriptors are released below.
        BuiltinContext context(*this, *m_command, m_args, m_stopRequested);
        status = m_command->Run(context);
    } catch (const std::exception& e) {
        LogError("BuiltinProcess '%s': %s failed: %s", m_path.c_str(), name.c_str(), e.what());
        if (auto err = m_io->GetDescriptor(IFileIO::kStdErr)) {
            const std::string line = name + ": internal error: " + e.what() + "\n";
            err->Write(line.data(), line.size());
        }
    } catch (...) {
        LogError("BuiltinProcess '%s': %s failed with an unknown error", m_path.c_str(), name.c_str());
    }
    LogDebug("BuiltinProcess '%s': %s exited with status %d", m_path.c_str(), name.c_str(), status);
    // Every descriptor this process opened is released before it reports
    // finished, so a pipe's reader sees end of file when its writer's program
    // ends.
    m_io->ReleaseAllDescriptors();
    {
        std::lock_guard<std::mutex> lock(m_finishedMutex);
        // The builtin's one ProcessEnd decision point (see ExitCodes.h): a
        // broken pipe beats a stop, which beats the command's own result. A
        // throw out of Run left status at 1, the same code a failed program
        // reports. The final flush in ~BuiltinContext has already run, so a
        // pipe found broken only when the buffered output went out is caught.
        m_exitCode = ExitCodeFor(
            m_brokenPipe ? ProcessEnd::BrokenPipe : m_stopRequested ? ProcessEnd::Stopped : ProcessEnd::Exited,
            status);
        m_finished = true;
    }
    m_finishedCv.notify_all();
}

uint64_t BuiltinProcess::GetPid() const {
    return m_pid;
}

uint64_t BuiltinProcess::GetParentPid() const {
    return m_parentPid;
}

std::string BuiltinProcess::Path() const {
    return m_path;
}

std::string BuiltinProcess::StartingAgentName() const {
    // A builtin is not an agent.
    return std::string();
}

std::shared_ptr<IEnvironment> BuiltinProcess::GetEnvironment() const {
    // A clone, as for every process: reading it from outside never changes it.
    return m_environment ? m_environment->Clone() : nullptr;
}

void BuiltinProcess::TriggerStop() {
    m_stopRequested = true;
    // Wake a pipe Read/Write the command is blocked in.
    m_stopToken->RequestStop();
}

void BuiltinProcess::StopForBrokenPipe() {
    m_brokenPipe = true;
    // Stopped the way TriggerStop would stop it -- quietly, nothing printed.
    TriggerStop();
}

bool BuiltinProcess::WaitToFinish(uint64_t timeoutMs) {
    // A wait for the command, made on the command's own thread, can only time
    // out: that thread is the one that would have to finish. Reported loudly,
    // as it is a sign of the problem DestroyOffRuntimeThreads.h describes.
    if (timeoutMs > 0 && IsOwnThread()) {
        LogError("BuiltinProcess '%s' pid=%llu: waiting %llums for itself on its own thread, which cannot finish while it waits",
            m_path.c_str(), static_cast<unsigned long long>(m_pid), static_cast<unsigned long long>(timeoutMs));
    }
    std::unique_lock<std::mutex> lock(m_finishedMutex);
    const bool finished = m_finishedCv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return m_finished.load(); });
    lock.unlock();
    if (finished) {
        std::lock_guard<std::mutex> joinLock(m_joinMutex);
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }
    return finished;
}

std::shared_ptr<IFileIO> BuiltinProcess::IO() const {
    return m_io;
}

std::shared_ptr<IAgent> BuiltinProcess::AsAgent() {
    return nullptr;
}

std::shared_ptr<IHaisosOS> BuiltinProcess::OS() const {
    return m_os.lock();
}

std::optional<int> BuiltinProcess::ExitCode() const {
    std::lock_guard<std::mutex> lock(m_finishedMutex);
    return m_exitCode;
}

} // namespace Haisos
