#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "interfaces/IHaisosOS.h"
#include "OSToolFactory.h"

namespace Haisos {

class HaisosOS : public IHaisosOS, public std::enable_shared_from_this<HaisosOS> {
public:
    HaisosOS(
        IFactory& factory,
        IServicesCreator& servicesCreator,
        std::shared_ptr<INetworkService> networkService,
        std::shared_ptr<ILLMService> llmService,
        std::unique_ptr<IFilesystemService> filesystemService,
        const std::string& rootPath,
        std::shared_ptr<IPhysicalConsole> physicalConsole,
        bool allowStartProcess,
        std::shared_ptr<std::atomic<uint64_t>> pidCounter);
    ~HaisosOS() override;

    std::shared_ptr<IProcess> StartProcess(
        const std::string& programPath,
        const std::vector<std::string>& args,
        std::shared_ptr<IAgent> callerAgent) override;
    std::vector<std::shared_ptr<IProcess>> GetRunningProcesses() const override;
    std::shared_ptr<IHaisosOS> CreateSubOS(const SubOSPermissions& permissions) override;
    IFilesystemService& GetFileSystemService() override;
    IServicesCreator& GetServicesCreator() override;

private:
    void CleanupFinishedProcesses();
    uint64_t ResolveParentPid(const std::shared_ptr<IAgent>& callerAgent) const;
    std::shared_ptr<IProcess> StartAgentProcess(const std::string& programPath, const std::vector<std::string>& args, uint64_t parentPid);
    std::shared_ptr<IProcess> StartLuaProcess(const std::string& programPath, const std::vector<std::string>& args, uint64_t parentPid);

    IFactory& m_factory;
    IServicesCreator& m_servicesCreator;
    std::shared_ptr<INetworkService> m_networkService;
    std::shared_ptr<ILLMService> m_llmService;
    std::unique_ptr<IFilesystemService> m_filesystemService;
    std::string m_rootPath;
    std::shared_ptr<IPhysicalConsole> m_physicalConsole;
    bool m_allowStartProcess;
    std::shared_ptr<std::atomic<uint64_t>> m_pidCounter;
    OSToolFactory m_osToolFactory;

    mutable std::mutex m_processesMutex;
    std::vector<std::shared_ptr<IProcess>> m_processes;

    mutable std::mutex m_agentPidMutex;
    std::unordered_map<IAgent*, uint64_t> m_agentToPid;
};

}
