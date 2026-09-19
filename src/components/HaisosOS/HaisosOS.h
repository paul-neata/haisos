#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include "interfaces/IFactory.h"
#include "interfaces/IHaisosOS.h"
#include "OSProcess.h"
#include "OSToolFactory.h"

namespace Haisos {

class HaisosOS : public IHaisosOS, public std::enable_shared_from_this<HaisosOS> {
public:
    // Builds an IHaisosOS. Not part of IHaisosOS's public surface: callers go
    // through IFactory::CreateHaisosOS (which allocates a fresh pid) or through
    // CreateSubOS (which passes this OS's own). The network and LLM services
    // are created internally from servicesCreator, the LLM ones configured from
    // `environment`.
    static std::shared_ptr<HaisosOS> Create(
        std::shared_ptr<IServicesCreator> servicesCreator,
        std::shared_ptr<IPhysicalConsole> physicalConsole,
        std::shared_ptr<IFileSystem> rootFileSystem,
        std::shared_ptr<IEnvironment> environment,
        uint64_t osProcessId);
    ~HaisosOS() override;

    std::shared_ptr<IProcess> StartProcess(
        std::shared_ptr<IEnvironment> environment,
        const std::string& programPath,
        const std::vector<std::string>& args,
        const std::string& workingDirectory) override;
    std::vector<std::shared_ptr<IProcess>> GetRunningProcesses() const override;
    std::shared_ptr<IHaisosOS> CreateSubOS(
        std::shared_ptr<IServicesCreator> servicesCreator,
        std::shared_ptr<IPhysicalConsole> physicalConsole,
        std::shared_ptr<IFileSystem> rootFileSystem,
        std::shared_ptr<IEnvironment> environment) override;
    std::shared_ptr<IFileSystem> GetRootFileSystem() override;
    std::shared_ptr<IServicesCreator> GetServicesCreator() override;
    std::shared_ptr<IEnvironment> GetOsEnvironment() const override;
    uint64_t GetOSProcessID() const override;

private:
    HaisosOS(
        std::shared_ptr<IServicesCreator> servicesCreator,
        std::shared_ptr<INetworkService> networkService,
        std::shared_ptr<ILLMService> llmService,
        std::shared_ptr<IFileSystem> rootFileSystem,
        std::shared_ptr<IPhysicalConsole> physicalConsole,
        std::shared_ptr<IEnvironment> environment,
        uint64_t osProcessId);

    // Erases finished processes. Must be called with m_processesMutex held.
    void CleanupFinishedProcesses();
    // Stops one process, escalating to Kill() if it does not stop in time.
    // Never called with m_processesMutex held.
    static void DrainProcess(const std::shared_ptr<IOSProcess>& process);
    std::shared_ptr<IOSProcess> StartAgentProcess(
        std::shared_ptr<IEnvironment> environment,
        const std::string& programPath,
        const std::vector<std::string>& args,
        const std::string& workingDirectory);
    std::shared_ptr<IOSProcess> StartLuaProcess(
        std::shared_ptr<IEnvironment> environment,
        const std::string& programPath,
        const std::vector<std::string>& args,
        const std::string& workingDirectory);

    std::shared_ptr<IServicesCreator> m_servicesCreator;
    std::shared_ptr<INetworkService> m_networkService;
    std::shared_ptr<ILLMService> m_llmService;
    std::shared_ptr<IFileSystem> m_rootFileSystem;
    std::shared_ptr<IPhysicalConsole> m_physicalConsole;
    std::shared_ptr<IEnvironment> m_environment;
    uint64_t m_osProcessId = 0;

    // Set by the destructor before draining, so no further process can be started.
    std::atomic<bool> m_shuttingDown{false};

    mutable std::mutex m_processesMutex;
    // Tracked as IOSProcess, not IProcess: the OS is the one thing that may
    // kill a process outright.
    std::vector<std::shared_ptr<IOSProcess>> m_processes;
};

}
