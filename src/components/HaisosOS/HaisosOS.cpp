#include "HaisosOS.h"
#include <algorithm>
#include <sstream>
#include "AgentProcess.h"
#include "LuaProcess.h"
#include "src/components/Console/AgentConsoleAdapter.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/libheaders/GloballyUniquePID.h"
#include "src/components/Logger/Logger.h"

namespace Haisos {

namespace {

std::string GetExtension(const std::string& path) {
    auto lastSlash = path.find_last_of("/\\");
    auto lastDot = path.find_last_of('.');
    if (lastDot == std::string::npos || (lastSlash != std::string::npos && lastDot < lastSlash)) {
        return "";
    }
    return path.substr(lastDot);
}

std::string GetStem(const std::string& path) {
    auto lastSlash = path.find_last_of("/\\");
    std::string base = (lastSlash == std::string::npos) ? path : path.substr(lastSlash + 1);
    auto lastDot = base.find_last_of('.');
    return (lastDot == std::string::npos) ? base : base.substr(0, lastDot);
}

// Maximum number of concurrently-running processes per OS instance. Each one
// costs a thread (plus, for an agent, an HTTP client and unbounded LLM spend),
// so an unbounded count is a fork bomb: a .lua script that starts itself, or an
// agent looping on os_start_process. The cap is a backstop against that runaway
// rather than a budget, so it is set well clear of any legitimate use.
constexpr size_t MAX_CONCURRENT_PROCESSES = 1024;

// Shutdown budget per process: how long a cooperative TriggerStop() is given
// before the OS stops waiting on it and moves on to the next one.
constexpr uint64_t PROCESS_STOP_TIMEOUT_MS = 5000;

// Processes are drained in passes, because a process started concurrently with
// shutdown can land in m_processes after the first snapshot was taken. The pass
// count is capped so the destructor can never spin forever.
constexpr int MAX_DRAIN_PASSES = 8;

} // namespace

HaisosOS::HaisosOS(
    std::shared_ptr<IServicesCreator> servicesCreator,
    std::shared_ptr<INetworkService> networkService,
    std::shared_ptr<ILLMService> llmService,
    std::shared_ptr<IFileSystem> rootFileSystem,
    std::shared_ptr<IPhysicalConsole> physicalConsole,
    std::shared_ptr<IEnvironment> environment,
    uint64_t osProcessId)
    : m_servicesCreator(std::move(servicesCreator))
    , m_networkService(std::move(networkService))
    , m_llmService(std::move(llmService))
    , m_rootFileSystem(std::move(rootFileSystem))
    , m_physicalConsole(std::move(physicalConsole))
    , m_environment(std::move(environment))
    , m_osProcessId(osProcessId)
{
}

HaisosOS::~HaisosOS() {
    // This destructor exists to stop and drain the tracked processes
    // deterministically -- with a bounded, escalating shutdown -- instead of
    // letting member destruction order decide when (and for how long) each
    // process is torn down. Draining is time-boxed: a process that ignores
    // Stop() is killed rather than waited on forever.
    m_shuttingDown = true;

    size_t initialCount = 0;
    {
        std::lock_guard<std::mutex> lock(m_processesMutex);
        initialCount = m_processes.size();
    }
    LogInfo("HaisosOS: shutting down, draining %zu running process(es)", initialCount);

    for (int pass = 0; pass < MAX_DRAIN_PASSES; ++pass) {
        // Declared inside the pass so it is destroyed at the end of one, which
        // is both outside the lock and after the draining: releasing a process
        // joins its thread, and that thread may call back into the OS.
        //
        // Never hold m_processesMutex while calling into a process: the swap
        // takes the whole list out under the lock, and the draining below runs
        // unlocked against this pass's private copy.
        std::vector<std::shared_ptr<ICurrentProcess>> processes;
        {
            std::lock_guard<std::mutex> lock(m_processesMutex);
            processes.swap(m_processes);
        }
        // Nothing left, and nothing arrived while the previous pass drained:
        // that is the only way this loop is meant to end.
        if (processes.empty()) {
            return;
        }
        for (const auto& process : processes) {
            DrainProcess(process);
        }
    }

    size_t stillTracked = 0;
    {
        std::lock_guard<std::mutex> lock(m_processesMutex);
        stillTracked = m_processes.size();
    }
    LogWarning("HaisosOS: gave up draining after %d passes with %zu process(es) still tracked; "
        "they are being destroyed without a stop having been waited for",
        MAX_DRAIN_PASSES, stillTracked);
}

void HaisosOS::DrainProcess(const std::shared_ptr<ICurrentProcess>& process) {
    // TriggerStop is all there is. It is only a request -- for an agent it
    // closes the command queue, so a command already in flight keeps running --
    // so the wait is bounded and the OS moves on rather than blocking shutdown
    // on one process. What actually waits the thread out is the process's own
    // destructor, which runs when the last reference to it is released.
    process->TriggerStop();
    if (!process->WaitToFinish(PROCESS_STOP_TIMEOUT_MS)) {
        LogWarning("HaisosOS: process '%s' did not stop within %llums during destruction; "
            "its destructor will wait for it",
            process->Path().c_str(), static_cast<unsigned long long>(PROCESS_STOP_TIMEOUT_MS));
    }
}

void HaisosOS::CleanupFinishedProcesses() {
    m_processes.erase(
        std::remove_if(m_processes.begin(), m_processes.end(),
            [](const std::shared_ptr<ICurrentProcess>& process) {
                // WaitToFinish(0) does not wait; it just reports whether the
                // process has finished.
                return process->WaitToFinish(0);
            }),
        m_processes.end());
}

std::shared_ptr<ICurrentProcess> HaisosOS::StartAgentProcess(
    std::shared_ptr<IEnvironment> environment,
    const std::string& programPath,
    const std::vector<std::string>& args,
    const std::string& workingDirectory)
{
    std::string content;
    if (!ReadWholeFile(*m_rootFileSystem, programPath, content)) {
        LogError("HaisosOS: failed to read process file: %s", programPath.c_str());
        return nullptr;
    }
    if (content.empty()) {
        LogError("HaisosOS: process file is empty: %s", programPath.c_str());
        return nullptr;
    }

    if (!args.empty()) {
        content += "\n\n--- Arguments ---\n";
        for (const auto& arg : args) {
            content += arg + "\n";
        }
    }
    // Deliberately not passed through SanitizeUserInput: this text is the agent's
    // own program, so every line of it is instructions by definition -- stripping
    // "injection" phrasing buys nothing against whoever wrote the file, while its
    // lossy rewriting (it deletes anything between '<' and '>', drops whole lines,
    // and caps at 64KB) would silently corrupt the program being run.
    uint64_t pid = NextGloballyUniquePID();
    std::string name = GetStem(programPath) + "_" + std::to_string(pid);

    auto console = AgentConsoleAdapter::Create(m_physicalConsole, name);
    // The OS tool set is built per process, around the process rather than
    // around this OS: ICurrentProcess is the only door out of a process (see
    // the Security section of the root CLAUDE.md). The handle exists before the
    // process does, because the agent needs its tools first.
    auto processHandle = CurrentProcessHandle::Create();
    // A process's agent is not interactive: it answers what its program asked
    // and then finishes, which is what makes the process finish too.
    auto agent = m_llmService->CreateAgent(
        name,
        /*parent=*/nullptr,
        std::move(console),
        OSToolFactory::Create(processHandle),
        {"You are a helpful AI assistant."},
        /*isInteractive=*/false);
    auto concreteAgent = std::dynamic_pointer_cast<Agent>(agent);
    if (!concreteAgent) {
        LogError("HaisosOS: the LLM service returned an agent this OS cannot own: %s", programPath.c_str());
        return nullptr;
    }

    // The parent is this OS. A process started here is top-most among
    // processes, but it is not parentless: the OS that started it is itself
    // identified by a pid from the same allocator, so naming it is both true
    // and more useful than 0, which means "no parent at all".
    // weak_from_this: the process reaches back to this OS for everything
    // outside itself, and an OS owns its processes, so the reference must not
    // be strong. A narrowed per-process OS would be passed here instead.
    auto process = AgentProcess::Create(
        pid, /*parentPid=*/m_osProcessId, std::move(environment), programPath, workingDirectory,
        weak_from_this(), processHandle, concreteAgent);
    if (!process) {
        // AgentProcess::Create has already said why. The agent is dropped here
        // unstarted: nothing was posted to it, and its destructor waits out the
        // thread Create() began.
        return nullptr;
    }

    // Only now: the agent's first command may call a tool, and a tool must find
    // the process it acts for, which AgentProcess::Create has just filled in.
    concreteAgent->Post(content);

    {
        std::lock_guard<std::mutex> lock(m_processesMutex);
        CleanupFinishedProcesses();
        m_processes.push_back(process);
    }
    LogInfo("HaisosOS: started agent process pid=%llu name='%s' program='%s'",
        static_cast<unsigned long long>(pid), name.c_str(), programPath.c_str());
    return process;
}

std::shared_ptr<ICurrentProcess> HaisosOS::StartLuaProcess(
    std::shared_ptr<IEnvironment> environment,
    const std::string& programPath,
    const std::vector<std::string>& args,
    const std::string& workingDirectory)
{
    std::string content;
    if (!ReadWholeFile(*m_rootFileSystem, programPath, content)) {
        LogError("HaisosOS: failed to read process file: %s", programPath.c_str());
        return nullptr;
    }

    uint64_t pid = NextGloballyUniquePID();
    std::string name = GetStem(programPath) + "_" + std::to_string(pid);

    auto console = AgentConsoleAdapter::Create(m_physicalConsole, name);
    // As for an agent process: the tool set is built around the process, and
    // LuaProcess::Create fills the handle in before the script's thread starts.
    auto processHandle = CurrentProcessHandle::Create();
    // The parent is this OS, as for an agent process above.
    auto process = LuaProcess::Create(
        pid, /*parentPid=*/m_osProcessId, std::move(environment), programPath, workingDirectory,
        weak_from_this(), processHandle, std::move(content), args,
        // The OS tool set and nothing else. The LLM tool set (get_current_date_time,
        // agent_*) belongs to agents: it is handed out by ILLMService and reaches
        // a process only through the agent running it, never through a script.
        OSToolFactory::Create(processHandle), std::move(console));

    {
        std::lock_guard<std::mutex> lock(m_processesMutex);
        CleanupFinishedProcesses();
        m_processes.push_back(process);
    }
    LogInfo("HaisosOS: started lua process pid=%llu name='%s' program='%s'",
        static_cast<unsigned long long>(pid), name.c_str(), programPath.c_str());
    return process;
}

std::shared_ptr<IProcess> HaisosOS::StartProcess(
    std::shared_ptr<IEnvironment> environment,
    const std::string& programPath,
    const std::vector<std::string>& args,
    const std::string& workingDirectory)
{
    // The environment is never taken from the OS behind the caller's back: a
    // process runs with what it was handed, so a missing one is a bug in the
    // caller rather than something to paper over with the OS's own.
    if (!environment) {
        LogError("HaisosOS: refusing to start '%s': no environment was passed", programPath.c_str());
        return nullptr;
    }

    if (m_shuttingDown) {
        LogWarning("HaisosOS: refusing to start '%s': this OS is shutting down", programPath.c_str());
        return nullptr;
    }

    // Cap concurrent processes, so a script that starts itself (or an agent
    // looping on os_start_process) cannot fork-bomb the OS.
    {
        std::lock_guard<std::mutex> lock(m_processesMutex);
        CleanupFinishedProcesses();
        if (m_processes.size() >= MAX_CONCURRENT_PROCESSES) {
            LogWarning("HaisosOS: refusing to start '%s': process limit of %zu concurrent processes reached",
                programPath.c_str(), MAX_CONCURRENT_PROCESSES);
            return nullptr;
        }
    }

    std::string extension = GetExtension(programPath);

    if (extension == ".md") {
        return StartAgentProcess(std::move(environment), programPath, args, workingDirectory);
    }
    if (extension == ".lua") {
        return StartLuaProcess(std::move(environment), programPath, args, workingDirectory);
    }
    LogWarning("HaisosOS: unsupported program type: %s", programPath.c_str());
    return nullptr;
}

std::vector<std::shared_ptr<IProcess>> HaisosOS::GetRunningProcesses() const {
    std::lock_guard<std::mutex> lock(m_processesMutex);
    // Handed out as IProcess: an outsider may look and ask, not kill.
    return std::vector<std::shared_ptr<IProcess>>(m_processes.begin(), m_processes.end());
}

std::shared_ptr<IHaisosOS> HaisosOS::CreateSubOS(
    std::shared_ptr<IServicesCreator> servicesCreator,
    std::shared_ptr<IPhysicalConsole> physicalConsole,
    std::shared_ptr<IFileSystem> rootFileSystem,
    std::shared_ptr<IEnvironment> environment)
{
    // A sub-OS is an ordinary OS; what confines it is the root filesystem the
    // caller hands it (typically this OS's root narrowed with
    // IFileSystemService::CreateSubFileSystem), not anything enforced here.
    // It carries this OS's pid: it is the same OS, seen through a narrower
    // root, not a new one belonging to some other process.
    return HaisosOS::Create(
        std::move(servicesCreator),
        std::move(physicalConsole),
        std::move(rootFileSystem),
        std::move(environment),
        m_osProcessId);
}

std::shared_ptr<IFileSystem> HaisosOS::GetRootFileSystem() {
    return m_rootFileSystem;
}

std::shared_ptr<IServicesCreator> HaisosOS::GetServicesCreator() {
    return m_servicesCreator;
}

std::shared_ptr<IEnvironment> HaisosOS::GetOsEnvironment() const {
    return m_environment;
}

uint64_t HaisosOS::GetOSProcessID() const {
    return m_osProcessId;
}

namespace {

std::string EnvValue(const IEnvironment& environment, const char* key) {
    return environment.GetVariable(key).value_or(std::string());
}

} // namespace

std::shared_ptr<HaisosOS> HaisosOS::Create(
    std::shared_ptr<IServicesCreator> servicesCreator,
    std::shared_ptr<IPhysicalConsole> physicalConsole,
    std::shared_ptr<IFileSystem> rootFileSystem,
    std::shared_ptr<IEnvironment> environment,
    uint64_t osProcessId)
{
    if (!environment) {
        LogError("HaisosOS: refusing to create an OS without an environment");
        return nullptr;
    }

    // The LLM configuration is part of the OS's environment rather than a set of
    // constructor parameters, so a sub-OS inherits it along with everything else.
    // There are deliberately no built-in defaults: the haisosfile's ENV
    // directives are the single source of truth, so the configuration a run used
    // is always readable from the haisosfile rather than half of it living here.
    std::string endpoint = EnvValue(*environment, kEnvEndpoint);
    std::string modelName = EnvValue(*environment, kEnvModel);
    std::string apiKey = EnvValue(*environment, kEnvApiKey);

    std::shared_ptr<INetworkService> networkService = servicesCreator->CreateNetworkService();
    std::shared_ptr<ILLMService> llmService = servicesCreator->CreateLLMService(networkService, endpoint, modelName, apiKey);

    return std::shared_ptr<HaisosOS>(new HaisosOS(
        std::move(servicesCreator),
        std::move(networkService),
        std::move(llmService),
        std::move(rootFileSystem),
        std::move(physicalConsole),
        std::move(environment),
        osProcessId));
}

}
