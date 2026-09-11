#include "HaisosOS.h"
#include <algorithm>
#include <filesystem>
#include "AgentProcess.h"
#include "LuaProcess.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/Logger/Logger.h"
#include "src/components/libheaders/SanitizeUserInput.h"

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

} // namespace

HaisosOS::HaisosOS(
    IFactory& factory,
    IServicesCreator& servicesCreator,
    std::shared_ptr<INetworkService> networkService,
    std::shared_ptr<ILLMService> llmService,
    std::unique_ptr<IFilesystemService> filesystemService,
    const std::string& rootPath,
    std::shared_ptr<IPhysicalConsole> physicalConsole,
    bool allowStartProcess,
    std::shared_ptr<std::atomic<uint64_t>> pidCounter)
    : m_factory(factory)
    , m_servicesCreator(servicesCreator)
    , m_networkService(std::move(networkService))
    , m_llmService(std::move(llmService))
    , m_filesystemService(std::move(filesystemService))
    , m_rootPath(rootPath)
    , m_physicalConsole(std::move(physicalConsole))
    , m_allowStartProcess(allowStartProcess)
    , m_pidCounter(std::move(pidCounter))
    , m_osToolFactory(*this, allowStartProcess)
{
}

HaisosOS::~HaisosOS() = default;

void HaisosOS::CleanupFinishedProcesses() {
    m_processes.erase(
        std::remove_if(m_processes.begin(), m_processes.end(),
            [](const std::shared_ptr<IProcess>& process) {
                return process->IsFinished();
            }),
        m_processes.end());
}

uint64_t HaisosOS::ResolveParentPid(const std::shared_ptr<IAgent>& callerAgent) const {
    if (!callerAgent) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(m_agentPidMutex);
    auto it = m_agentToPid.find(callerAgent.get());
    return it == m_agentToPid.end() ? 0 : it->second;
}

std::shared_ptr<IProcess> HaisosOS::StartAgentProcess(const std::string& programPath, const std::vector<std::string>& args, uint64_t parentPid) {
    std::string content;
    if (!ReadWholeFile(m_filesystemService->GetFileSystem(), programPath, content)) {
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
    content = SanitizeUserInput(content);

    uint64_t pid = m_pidCounter->fetch_add(1);
    std::string name = GetStem(programPath) + "_" + std::to_string(pid);

    auto console = m_factory.CreateAgentConsoleFromPhysical(m_physicalConsole, name);
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
        m_agentToPid[agent.get()] = pid;
    }

    agent->Post(content);

    auto process = std::make_shared<AgentProcess>(pid, parentPid, name, agent);

    std::lock_guard<std::mutex> lock(m_processesMutex);
    CleanupFinishedProcesses();
    m_processes.push_back(process);
    return process;
}

std::shared_ptr<IProcess> HaisosOS::StartLuaProcess(const std::string& programPath, const std::vector<std::string>& args, uint64_t parentPid) {
    std::string content;
    if (!ReadWholeFile(m_filesystemService->GetFileSystem(), programPath, content)) {
        LogError("HaisosOS: failed to read process file: %s", programPath.c_str());
        return nullptr;
    }

    uint64_t pid = m_pidCounter->fetch_add(1);
    std::string name = GetStem(programPath) + "_" + std::to_string(pid);

    auto console = m_factory.CreateAgentConsoleFromPhysical(m_physicalConsole, name);
    auto process = std::make_shared<LuaProcess>(pid, parentPid, name, std::move(content), args, m_osToolFactory, std::move(console));

    std::lock_guard<std::mutex> lock(m_processesMutex);
    CleanupFinishedProcesses();
    m_processes.push_back(process);
    return process;
}

std::shared_ptr<IProcess> HaisosOS::StartProcess(
    const std::string& programPath,
    const std::vector<std::string>& args,
    std::shared_ptr<IAgent> callerAgent)
{
    if (!m_allowStartProcess) {
        LogError("HaisosOS: starting processes is not permitted on this OS");
        return nullptr;
    }

    uint64_t parentPid = ResolveParentPid(callerAgent);
    std::string extension = GetExtension(programPath);

    if (extension == ".md") {
        return StartAgentProcess(programPath, args, parentPid);
    }
    if (extension == ".lua") {
        return StartLuaProcess(programPath, args, parentPid);
    }
    LogError("HaisosOS: unsupported program type: %s", programPath.c_str());
    return nullptr;
}

std::vector<std::shared_ptr<IProcess>> HaisosOS::GetRunningProcesses() const {
    std::lock_guard<std::mutex> lock(m_processesMutex);
    return m_processes;
}

std::shared_ptr<IHaisosOS> HaisosOS::CreateSubOS(const SubOSPermissions& permissions) {
    std::string subRootPath = permissions.subRootRelativePath.empty()
        ? m_rootPath
        : (std::filesystem::path(m_rootPath) / permissions.subRootRelativePath).string();

    // PhysicalFileSystem itself rejects a subRootPath that resolves outside
    // m_rootPath (it validates every path against whatever root it is
    // constructed with -- but that check happens per-operation, not at
    // construction time here). Validate up front so a bad sub-root fails loudly
    // at CreateSubOS() rather than silently on first use.
    std::string normalizedRoot = std::filesystem::weakly_canonical(std::filesystem::absolute(m_rootPath)).string();
    std::string normalizedSubRoot = std::filesystem::weakly_canonical(std::filesystem::absolute(subRootPath)).string();
    if (normalizedSubRoot.size() < normalizedRoot.size() ||
        normalizedSubRoot.compare(0, normalizedRoot.size(), normalizedRoot) != 0) {
        LogError("HaisosOS: sub-OS root escapes parent root: %s", permissions.subRootRelativePath.c_str());
        return nullptr;
    }

    auto subFilesystem = m_factory.CreatePhysicalFileSystem(subRootPath);
    auto subFilesystemService = m_servicesCreator.CreateFileSystemService(std::move(subFilesystem));

    return std::make_shared<HaisosOS>(
        m_factory,
        m_servicesCreator,
        m_networkService,
        m_llmService,
        std::move(subFilesystemService),
        normalizedSubRoot,
        m_physicalConsole,
        m_allowStartProcess && permissions.allowStartProcess,
        m_pidCounter);
}

IFilesystemService& HaisosOS::GetFileSystemService() {
    return *m_filesystemService;
}

IServicesCreator& HaisosOS::GetServicesCreator() {
    return m_servicesCreator;
}

std::shared_ptr<IHaisosOS> CreateHaisosOS(
    IFactory& factory,
    IServicesCreator& servicesCreator,
    std::shared_ptr<INetworkService> networkService,
    std::shared_ptr<ILLMService> llmService,
    std::unique_ptr<IFilesystemService> filesystemService,
    const std::string& rootPath,
    std::shared_ptr<IPhysicalConsole> physicalConsole,
    bool allowStartProcess)
{
    std::string normalizedRoot = std::filesystem::weakly_canonical(std::filesystem::absolute(rootPath)).string();
    return std::make_shared<HaisosOS>(
        factory,
        servicesCreator,
        std::move(networkService),
        std::move(llmService),
        std::move(filesystemService),
        normalizedRoot,
        std::move(physicalConsole),
        allowStartProcess,
        std::make_shared<std::atomic<uint64_t>>(1));
}

}
