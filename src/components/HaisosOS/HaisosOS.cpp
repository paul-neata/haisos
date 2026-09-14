#include "HaisosOS.h"
#include <algorithm>
#include <sstream>
#include "AgentProcess.h"
#include "LuaProcess.h"
#include "src/components/Console/AgentConsoleAdapter.h"
#include "src/components/Filesystem/FilesystemUtils.h"
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
    std::unique_ptr<IFilesystemService> filesystemServiceFactory,
    std::shared_ptr<IFileSystem> rootFileSystem,
    std::shared_ptr<IPhysicalConsole> physicalConsole,
    OSEnvironment environment,
    uint64_t osProcessId)
    : m_servicesCreator(std::move(servicesCreator))
    , m_networkService(std::move(networkService))
    , m_llmService(std::move(llmService))
    , m_filesystemServiceFactory(std::move(filesystemServiceFactory))
    , m_rootFileSystem(std::move(rootFileSystem))
    , m_physicalConsole(std::move(physicalConsole))
    , m_environment(std::move(environment))
    , m_osProcessId(osProcessId)
    , m_osToolFactory(*this)
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
    std::vector<std::shared_ptr<IProcess>> processes;
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

void HaisosOS::DrainProcess(const std::shared_ptr<IProcess>& process) {
    // Stop(0) is only a request to stop (for an agent it merely closes the
    // command queue, so a command already in flight keeps running), hence the
    // bounded wait and the escalation to Kill() below.
    process->Stop(0);
    if (process->WaitToFinish(PROCESS_STOP_TIMEOUT_MS)) {
        return;
    }
    LogWarning("HaisosOS: process '%s' did not stop within %llums during destruction, killing it",
        process->Name().c_str(), static_cast<unsigned long long>(PROCESS_STOP_TIMEOUT_MS));
    process->Kill();
    if (!process->WaitToFinish(PROCESS_KILL_TIMEOUT_MS)) {
        LogError("HaisosOS: process '%s' did not finish within %llums after being killed, abandoning the wait",
            process->Name().c_str(), static_cast<unsigned long long>(PROCESS_KILL_TIMEOUT_MS));
    }
}

void HaisosOS::CleanupFinishedProcesses() {
    std::vector<IAgent*> finishedAgents;
    m_processes.erase(
        std::remove_if(m_processes.begin(), m_processes.end(),
            [&finishedAgents](const std::shared_ptr<IProcess>& process) {
                if (!process->IsFinished()) {
                    return false;
                }
                if (auto agent = process->AsAgent()) {
                    finishedAgents.push_back(agent.get());
                }
                return true;
            }),
        m_processes.end());

    if (finishedAgents.empty()) {
        return;
    }
    // Called with m_processesMutex held; m_agentPidMutex is only ever taken
    // nested under it, never the other way around.
    std::lock_guard<std::mutex> lock(m_agentPidMutex);
    for (IAgent* agent : finishedAgents) {
        m_agentToPid.erase(agent);
    }
}

uint64_t HaisosOS::ResolveParentPid(const std::shared_ptr<IAgent>& callerAgent) const {
    if (!callerAgent) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(m_agentPidMutex);
    auto it = m_agentToPid.find(callerAgent.get());
    if (it == m_agentToPid.end()) {
        return 0;
    }
    // Guard against a recycled address: only trust the entry if it still
    // refers to this very agent instance, not to a dead one it outlived.
    if (it->second.agent.lock() != callerAgent) {
        return 0;
    }
    return it->second.pid;
}

std::shared_ptr<IProcess> HaisosOS::StartAgentProcess(const std::string& programPath, const std::vector<std::string>& args, uint64_t parentPid) {
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
    uint64_t pid = AllocateUniquePid();
    std::string name = GetStem(programPath) + "_" + std::to_string(pid);

    auto console = std::make_unique<AgentConsoleAdapter>(m_physicalConsole, name);
    auto agent = m_llmService->CreateAgent(
        {"You are a helpful AI assistant."},
        name,
        nullptr,
        std::move(console),
        "",
        false,
        &m_osToolFactory);

    {
        std::lock_guard<std::mutex> lock(m_agentPidMutex);
        m_agentToPid[agent.get()] = AgentPidEntry{agent, pid};
    }

    agent->Post(content);

    auto process = std::make_shared<AgentProcess>(pid, parentPid, name, agent);

    {
        std::lock_guard<std::mutex> lock(m_processesMutex);
        CleanupFinishedProcesses();
        m_processes.push_back(process);
    }
    LogInfo("HaisosOS: started agent process pid=%llu parent_pid=%llu name='%s' program='%s'",
        static_cast<unsigned long long>(pid), static_cast<unsigned long long>(parentPid), name.c_str(), programPath.c_str());
    return process;
}

std::shared_ptr<IProcess> HaisosOS::StartLuaProcess(const std::string& programPath, const std::vector<std::string>& args, uint64_t parentPid) {
    std::string content;
    if (!ReadWholeFile(*m_rootFileSystem, programPath, content)) {
        LogError("HaisosOS: failed to read process file: %s", programPath.c_str());
        return nullptr;
    }

    uint64_t pid = AllocateUniquePid();
    std::string name = GetStem(programPath) + "_" + std::to_string(pid);

    auto console = std::make_shared<AgentConsoleAdapter>(m_physicalConsole, name);
    auto process = std::make_shared<LuaProcess>(pid, parentPid, name, std::move(content), args, m_osToolFactory, console);

    {
        std::lock_guard<std::mutex> lock(m_processesMutex);
        CleanupFinishedProcesses();
        m_processes.push_back(process);
    }
    LogInfo("HaisosOS: started lua process pid=%llu parent_pid=%llu name='%s' program='%s'",
        static_cast<unsigned long long>(pid), static_cast<unsigned long long>(parentPid), name.c_str(), programPath.c_str());
    return process;
}

std::shared_ptr<IProcess> HaisosOS::StartProcess(
    const std::string& programPath,
    const std::vector<std::string>& args,
    std::shared_ptr<IAgent> callerAgent)
{
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

    uint64_t parentPid = ResolveParentPid(callerAgent);
    std::string extension = GetExtension(programPath);

    if (extension == ".md") {
        return StartAgentProcess(programPath, args, parentPid);
    }
    if (extension == ".lua") {
        return StartLuaProcess(programPath, args, parentPid);
    }
    LogWarning("HaisosOS: unsupported program type: %s", programPath.c_str());
    return nullptr;
}

std::vector<std::shared_ptr<IProcess>> HaisosOS::GetRunningProcesses() const {
    std::lock_guard<std::mutex> lock(m_processesMutex);
    return m_processes;
}

std::shared_ptr<IHaisosOS> HaisosOS::CreateSubOS(
    std::shared_ptr<IServicesCreator> servicesCreator,
    std::shared_ptr<IPhysicalConsole> physicalConsole,
    std::shared_ptr<IFileSystem> rootFileSystem,
    const OSEnvironment& environment,
    uint64_t osProcessId)
{
    // A sub-OS is an ordinary OS; what confines it is the root filesystem the
    // caller hands it (typically this OS's root narrowed with
    // IFilesystemService::CreateSubFileSystem), not anything enforced here.
    return ::Haisos::CreateHaisosOS(
        std::move(servicesCreator),
        std::move(physicalConsole),
        std::move(rootFileSystem),
        environment,
        osProcessId);
}

IFileSystem& HaisosOS::GetRootFileSystem() {
    return *m_rootFileSystem;
}

IFilesystemService& HaisosOS::GetFileSystemService() {
    return *m_filesystemServiceFactory;
}

IServicesCreator& HaisosOS::GetServicesCreator() {
    return *m_servicesCreator;
}

const OSEnvironment& HaisosOS::GetOsEnvironment() const {
    return m_environment;
}

uint64_t HaisosOS::GetOSProcessID() const {
    return m_osProcessId;
}

uint64_t AllocateUniquePid() {
    // One allocator for the whole program, so a pid identifies a process (and,
    // through GetOSProcessID(), the OS it owns) uniquely no matter which OS tree
    // it came from. Starts at 1: 0 means "no parent"/"the initial OS".
    static std::atomic<uint64_t> counter{1};
    return counter.fetch_add(1);
}

namespace {

std::string EnvValue(const OSEnvironment& environment, const char* key) {
    auto it = environment.find(key);
    return (it == environment.end()) ? std::string() : it->second;
}

} // namespace

std::shared_ptr<IHaisosOS> CreateHaisosOS(
    std::shared_ptr<IServicesCreator> servicesCreator,
    std::shared_ptr<IPhysicalConsole> physicalConsole,
    std::shared_ptr<IFileSystem> rootFileSystem,
    const OSEnvironment& environment,
    uint64_t osProcessId)
{
    // The LLM configuration is part of the OS's environment rather than a set of
    // constructor parameters, so a sub-OS inherits it along with everything else.
    // There are deliberately no built-in defaults: the haisosfile's ENV
    // directives are the single source of truth, so the configuration a run used
    // is always readable from the haisosfile rather than half of it living here.
    std::string endpoint = EnvValue(environment, kEnvEndpoint);
    std::string modelName = EnvValue(environment, kEnvModel);
    std::string apiKey = EnvValue(environment, kEnvApiKey);

    if (endpoint.empty() || modelName.empty()) {
        LogWarning(
            "HaisosOS: %s is not set in the OS environment; agents will fail to reach an LLM. "
            "Set it in the haisosfile with `ENV %s=<value>`, or import the host's with `ENV %s`.",
            endpoint.empty() ? kEnvEndpoint : kEnvModel,
            endpoint.empty() ? kEnvEndpoint : kEnvModel,
            endpoint.empty() ? kEnvEndpoint : kEnvModel);
    }

    std::shared_ptr<INetworkService> networkService = servicesCreator->CreateNetworkService();
    std::shared_ptr<ILLMService> llmService = servicesCreator->CreateLLMService(*networkService, endpoint, modelName, apiKey);
    auto filesystemServiceFactory = servicesCreator->CreateFileSystemService();

    return std::make_shared<HaisosOS>(
        std::move(servicesCreator),
        std::move(networkService),
        std::move(llmService),
        std::move(filesystemServiceFactory),
        std::move(rootFileSystem),
        std::move(physicalConsole),
        environment,
        osProcessId);
}

}
