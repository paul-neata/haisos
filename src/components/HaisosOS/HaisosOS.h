#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include "interfaces/IFactory.h"
#include "interfaces/IHaisosOS.h"
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
        std::shared_ptr<IBuiltinCommands> builtinCommands,
        std::shared_ptr<IEnvironment> environment,
        uint64_t osProcessId);
    ~HaisosOS() override;

    std::shared_ptr<IProcess> StartProcess(
        std::shared_ptr<IEnvironment> environment,
        const std::string& programPath,
        const std::vector<std::string>& args,
        const std::string& workingDirectory,
        const StartProcessOptions& options) override;
    std::vector<std::shared_ptr<IProcess>> GetRunningProcesses() const override;
    std::shared_ptr<IHaisosOS> CreateSubOS(
        std::shared_ptr<IServicesCreator> servicesCreator,
        std::shared_ptr<IPhysicalConsole> physicalConsole,
        std::shared_ptr<IFileSystem> rootFileSystem,
        std::shared_ptr<IBuiltinCommands> builtinCommands,
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
        std::shared_ptr<IBuiltinCommands> builtinCommands,
        std::shared_ptr<IPhysicalConsole> physicalConsole,
        std::shared_ptr<IEnvironment> environment,
        uint64_t osProcessId);

    // Erases finished processes. Must be called with m_processesMutex held.
    void CleanupFinishedProcesses();
    // Asks one process to stop and waits a bounded time for it.
    // Never called with m_processesMutex held.
    static void DrainProcess(const std::shared_ptr<IProcess>& process);
    std::shared_ptr<ICurrentProcess> StartAgentProcess(
        std::shared_ptr<IEnvironment> environment,
        const std::string& programPath,
        const std::vector<std::string>& args,
        const std::string& workingDirectory,
        bool interactive);
    std::shared_ptr<IProcess> StartBuiltinProcess(
        std::shared_ptr<IEnvironment> environment,
        const std::string& programPath,
        const std::string& builtinName,
        const std::vector<std::string>& args,
        const std::string& workingDirectory,
        const StartProcessOptions& options);
    std::shared_ptr<ICurrentProcess> StartLuaProcess(
        std::shared_ptr<IEnvironment> environment,
        const std::string& programPath,
        const std::vector<std::string>& args,
        const std::string& workingDirectory);

    std::shared_ptr<IServicesCreator> m_servicesCreator;
    std::shared_ptr<INetworkService> m_networkService;
    std::shared_ptr<ILLMService> m_llmService;
    std::shared_ptr<IFileSystem> m_rootFileSystem;
    // Deliberately not exposed: what runs this OS's builtins is its own
    // business. May be null, for an OS that runs none.
    std::shared_ptr<IBuiltinCommands> m_builtinCommands;
    std::shared_ptr<IPhysicalConsole> m_physicalConsole;
    std::shared_ptr<IEnvironment> m_environment;
    uint64_t m_osProcessId = 0;

    // Set by the destructor before draining, so no further process can be started.
    std::atomic<bool> m_shuttingDown{false};

    mutable std::mutex m_processesMutex;
    // Tracked by their outside view, IProcess: that is all the OS ever asks of
    // them (stop, wait, name), and all a builtin's process is handed back as.
    std::vector<std::shared_ptr<IProcess>> m_processes;
};

}
