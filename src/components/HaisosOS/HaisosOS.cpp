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
// agent looping on os_start_process. 64 is far above any legitimate use (a
// handful of initial RUNs plus their children, with agent recursion already
// capped at depth 5) while keeping thread and socket usage bounded.
constexpr size_t MAX_CONCURRENT_PROCESSES = 64;

// Shutdown budget per process: how long a cooperative Stop() is given before
// escalating to Kill(), and how long Kill() is then given to take effect.
constexpr uint64_t PROCESS_STOP_TIMEOUT_MS = 5000;
constexpr uint64_t PROCESS_KILL_TIMEOUT_MS = 2000;

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

    // Never hold m_processesMutex while calling into a process: take the
    // processes out of the list under the lock, then drain them unlocked.
    std::vector<std::shared_ptr<IOSProcess>> processes;
    for (int pass = 0; pass < MAX_DRAIN_PASSES; ++pass) {
        // Release the previous pass's processes outside the lock: destroying a
        // process can join its thread, which may itself call back into the OS.
        processes.clear();
        {
            std::lock_guard<std::mutex> lock(m_processesMutex);
            processes.swap(m_processes);
        }
        if (processes.empty()) {
            return;
        }
        for (const auto& process : processes) {
            DrainProcess(process);
        }
    }
    LogWarning("HaisosOS: processes were still being started during shutdown after %d drain passes", MAX_DRAIN_PASSES);
}

void HaisosOS::DrainProcess(const std::shared_ptr<IOSProcess>& process) {
    // TriggerStop is only a request (for an agent it merely closes the command
    // queue, so a command already in flight keeps running), hence the bounded
    // wait and the escalation to Kill() below -- which is on IOSProcess rather
    // than IProcess, because forcing a process down is the OS's business.
    process->TriggerStop();
    if (process->WaitToFinish(PROCESS_STOP_TIMEOUT_MS)) {
        return;
    }
    LogWarning("HaisosOS: process '%s' did not stop within %llums during destruction, killing it",
        process->Path().c_str(), static_cast<unsigned long long>(PROCESS_STOP_TIMEOUT_MS));
    process->Kill();
    if (!process->WaitToFinish(PROCESS_KILL_TIMEOUT_MS)) {
        LogError("HaisosOS: process '%s' did not finish within %llums after being killed, abandoning the wait",
            process->Path().c_str(), static_cast<unsigned long long>(PROCESS_KILL_TIMEOUT_MS));
    }
}

void HaisosOS::CleanupFinishedProcesses() {
    m_processes.erase(
        std::remove_if(m_processes.begin(), m_processes.end(),
            [](const std::shared_ptr<IOSProcess>& process) {
                // WaitToFinish(0) does not wait; it just reports whether the
                // process has finished.
                return process->WaitToFinish(0);
            }),
        m_processes.end());
}

std::shared_ptr<IOSProcess> HaisosOS::StartAgentProcess(
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

    // Parent pid 0: a process started here is top-most. Parentage used to be
    // resolved from the calling agent, but a process is opaque -- whether it is
    // an agent is its own business -- and the process/agent relationship is
    // being redefined, so nothing claims to know a parent for now.
    // weak_from_this: the process reaches back to this OS for everything
    // outside itself, and an OS owns its processes, so the reference must not
    // be strong. A narrowed per-process OS would be passed here instead.
    auto process = AgentProcess::Create(
        pid, /*parentPid=*/0, std::move(environment), programPath, workingDirectory,
        m_rootFileSystem, weak_from_this(), processHandle, concreteAgent);

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

std::shared_ptr<IOSProcess> HaisosOS::StartLuaProcess(
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
    auto process = LuaProcess::Create(
        pid, /*parentPid=*/0, std::move(environment), programPath, workingDirectory,
        m_rootFileSystem, weak_from_this(), processHandle, std::move(content), args,
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

    if (endpoint.empty() || modelName.empty()) {
        LogWarning(
            "HaisosOS: %s is not set in the OS environment; agents will fail to reach an LLM. "
            "Set it in the haisosfile with `ENV %s=<value>`, or import the host's with `ENV %s`.",
            endpoint.empty() ? kEnvEndpoint : kEnvModel,
            endpoint.empty() ? kEnvEndpoint : kEnvModel,
            endpoint.empty() ? kEnvEndpoint : kEnvModel);
    }

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
