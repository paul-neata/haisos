#include "BuiltinProcess.h"
#include <chrono>
#include "ProcessFileIO.h"
#include "src/components/Logger/Logger.h"

namespace Haisos {

std::shared_ptr<BuiltinProcess> BuiltinProcess::Create(
    const BuiltinCommandHost& host,
    std::shared_ptr<IEnvironment> environment,
    std::shared_ptr<IBuiltinCommand> command,
    std::vector<std::string> args,
    const std::string& workingDirectory)
{
    auto process = std::shared_ptr<BuiltinProcess>(new BuiltinProcess(
        host, std::move(environment), std::move(command), std::move(args), workingDirectory));
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
    , m_console(host.console)
    , m_command(std::move(command))
    , m_args(std::move(args))
{
}

BuiltinProcess::~BuiltinProcess() {
    TriggerStop();
    std::lock_guard<std::mutex> joinLock(m_joinMutex);
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void BuiltinProcess::Start() {
    m_thread = std::thread(&BuiltinProcess::RunThread, this);
}

void BuiltinProcess::RunThread() {
    const std::string name = m_command->Name();
    int status = 1;
    try {
        BuiltinContext context(*this, *m_command, m_args, m_console, m_stopRequested);
        status = m_command->Run(context);
    } catch (const std::exception& e) {
        LogError("BuiltinProcess '%s': %s failed: %s", m_path.c_str(), name.c_str(), e.what());
        if (m_console) {
            m_console->Write(name + ": internal error: " + e.what());
        }
    } catch (...) {
        LogError("BuiltinProcess '%s': %s failed with an unknown error", m_path.c_str(), name.c_str());
    }
    m_exitStatus = status;
    LogDebug("BuiltinProcess '%s': %s exited with status %d", m_path.c_str(), name.c_str(), status);
    {
        std::lock_guard<std::mutex> lock(m_finishedMutex);
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
}

bool BuiltinProcess::WaitToFinish(uint64_t timeoutMs) {
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

int BuiltinProcess::ExitStatus() const {
    return m_exitStatus.load();
}

} // namespace Haisos
