#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "interfaces/IFactory.h"
#include "interfaces/IHaisosOS.h"
#include "OSToolFactory.h"

namespace Haisos {

class HaisosOS : public IHaisosOS, public std::enable_shared_from_this<HaisosOS> {
public:
    HaisosOS(
        std::shared_ptr<IServicesCreator> servicesCreator,
        std::shared_ptr<INetworkService> networkService,
        std::shared_ptr<ILLMService> llmService,
        std::unique_ptr<IFilesystemService> filesystemServiceFactory,
        std::shared_ptr<IFileSystem> rootFileSystem,
        std::shared_ptr<IPhysicalConsole> physicalConsole,
        OSEnvironment environment,
        uint64_t osProcessId);
    ~HaisosOS() override;

    std::shared_ptr<IProcess> StartProcess(
        const std::string& programPath,
        const std::vector<std::string>& args,
        std::shared_ptr<IAgent> callerAgent) override;
    std::vector<std::shared_ptr<IProcess>> GetRunningProcesses() const override;
    std::shared_ptr<IHaisosOS> CreateSubOS(
        std::shared_ptr<IServicesCreator> servicesCreator,
        std::shared_ptr<IPhysicalConsole> physicalConsole,
        std::shared_ptr<IFileSystem> rootFileSystem,
        const OSEnvironment& environment,
        uint64_t osProcessId) override;
    IFileSystem& GetRootFileSystem() override;
    IFilesystemService& GetFileSystemService() override;
    IServicesCreator& GetServicesCreator() override;
    const OSEnvironment& GetOsEnvironment() const override;
    uint64_t GetOSProcessID() const override;

private:
    // Erases finished processes (and their m_agentToPid entries).
    // Must be called with m_processesMutex held.
    void CleanupFinishedProcesses();
    // Stops one process, escalating to Kill() if it does not stop in time.
    // Never called with m_processesMutex held.
    static void DrainProcess(const std::shared_ptr<IProcess>& process);
    uint64_t ResolveParentPid(const std::shared_ptr<IAgent>& callerAgent) const;
    std::shared_ptr<IProcess> StartAgentProcess(const std::string& programPath, const std::vector<std::string>& args, uint64_t parentPid);
    std::shared_ptr<IProcess> StartLuaProcess(const std::string& programPath, const std::vector<std::string>& args, uint64_t parentPid);

    std::shared_ptr<IServicesCreator> m_servicesCreator;
    std::shared_ptr<INetworkService> m_networkService;
    std::shared_ptr<ILLMService> m_llmService;
    std::unique_ptr<IFilesystemService> m_filesystemServiceFactory;
    std::shared_ptr<IFileSystem> m_rootFileSystem;
    std::shared_ptr<IPhysicalConsole> m_physicalConsole;
    OSEnvironment m_environment;
    uint64_t m_osProcessId = 0;
    OSToolFactory m_osToolFactory;

    // Set by the destructor before draining, so no further process can be started.
    std::atomic<bool> m_shuttingDown{false};

    mutable std::mutex m_processesMutex;
    std::vector<std::shared_ptr<IProcess>> m_processes;

    // A pid keyed by the agent's address, kept honest by a weak_ptr: an address
    // can be reused by a later allocation, so an entry counts only while it
    // still refers to the very agent instance that registered it.
    struct AgentPidEntry {
        std::weak_ptr<IAgent> agent;
        uint64_t pid = 0;
    };

    mutable std::mutex m_agentPidMutex;
    std::unordered_map<IAgent*, AgentPidEntry> m_agentToPid;
};

// Builds a root IHaisosOS. Not part of IHaisosOS's public surface: callers go
// through IFactory::CreateHaisosOS, which forwards here. The network/LLM/
// filesystem services are created internally from servicesCreator, the LLM ones
// configured from `environment`.
std::shared_ptr<IHaisosOS> CreateHaisosOS(
    std::shared_ptr<IServicesCreator> servicesCreator,
    std::shared_ptr<IPhysicalConsole> physicalConsole,
    std::shared_ptr<IFileSystem> rootFileSystem,
    const OSEnvironment& environment,
    uint64_t osProcessId);

}
