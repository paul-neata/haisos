#include "HaisosOS.h"
#include <algorithm>
#include <sstream>
#include "AgentProcess.h"
#include "LuaProcess.h"
#include "src/components/Console/AgentConsoleAdapter.h"
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
    IServicesCreator& servicesCreator,
    std::shared_ptr<INetworkService> networkService,
    std::shared_ptr<ILLMService> llmService,
    std::unique_ptr<IFilesystemService> filesystemServiceFactory,
    std::shared_ptr<IFileSystem> rootFileSystem,
    std::shared_ptr<IPhysicalConsole> physicalConsole,
    bool allowStartProcess,
    std::shared_ptr<std::atomic<uint64_t>> pidCounter)
    : m_servicesCreator(servicesCreator)
    , m_networkService(std::move(networkService))
    , m_llmService(std::move(llmService))
    , m_filesystemServiceFactory(std::move(filesystemServiceFactory))
    , m_rootFileSystem(std::move(rootFileSystem))
    , m_physicalConsole(std::move(physicalConsole))
    , m_allowStartProcess(allowStartProcess)
    , m_pidCounter(std::move(pidCounter))
    , m_osToolFactory(*this, allowStartProcess)
{
}

HaisosOS::~HaisosOS() {
    // Stop and drain every tracked process before any other member (in
    // particular m_osToolFactory, which agent-backed processes' tool
    // factories reference) is torn down, so no process can outlive its
    // owning HaisosOS.
    std::vector<std::shared_ptr<IProcess>> processes;
    {
        std::lock_guard<std::mutex> lock(m_processesMutex);
        processes = m_processes;
    }
    for (const auto& process : processes) {
        process->Stop(0);
        if (!process->WaitToFinish(5000)) {
            LogWarning("HaisosOS: process '%s' did not finish within 5s during destruction, waiting indefinitely", process->Name().c_str());
        }
        process->WaitToFinish();
    }
}

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
    content = SanitizeUserInput(content);

    uint64_t pid = m_pidCounter->fetch_add(1);
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
    if (!ReadWholeFile(*m_rootFileSystem, programPath, content)) {
        LogError("HaisosOS: failed to read process file: %s", programPath.c_str());
        return nullptr;
    }

    uint64_t pid = m_pidCounter->fetch_add(1);
    std::string name = GetStem(programPath) + "_" + std::to_string(pid);

    auto console = std::make_shared<AgentConsoleAdapter>(m_physicalConsole, name);
    auto process = std::make_shared<LuaProcess>(pid, parentPid, name, std::move(content), args, m_osToolFactory, console);

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
        LogWarning("HaisosOS: starting processes is not permitted on this OS");
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
    LogWarning("HaisosOS: unsupported program type: %s", programPath.c_str());
    return nullptr;
}

std::vector<std::shared_ptr<IProcess>> HaisosOS::GetRunningProcesses() const {
    std::lock_guard<std::mutex> lock(m_processesMutex);
    return m_processes;
}

std::shared_ptr<IHaisosOS> HaisosOS::CreateSubOS(const SubOSPermissions& permissions) {
    // SubFileSystem itself cannot actually escape (it clamps a climbing ".."
    // at its own virtual root), but a caller that passed a path escaping this
    // OS's root almost certainly made a mistake and wants an error, not a
    // silent reinterpretation as some other sub-path -- so reject it here.
    if (!permissions.subRootRelativePath.empty()) {
        std::istringstream iss(permissions.subRootRelativePath);
        std::string segment;
        int depth = 0;
        while (std::getline(iss, segment, '/')) {
            if (segment.empty() || segment == ".") {
                continue;
            }
            if (segment == "..") {
                if (depth == 0) {
                    LogWarning("HaisosOS: sub-OS root escapes parent root: %s", permissions.subRootRelativePath.c_str());
                    return nullptr;
                }
                --depth;
            } else {
                ++depth;
            }
        }
    }

    std::shared_ptr<IFileSystem> subFileSystem = permissions.subRootRelativePath.empty()
        ? m_rootFileSystem
        : m_filesystemServiceFactory->CreateSubFileSystem(m_rootFileSystem, permissions.subRootRelativePath);

    return std::make_shared<HaisosOS>(
        m_servicesCreator,
        m_networkService,
        m_llmService,
        m_servicesCreator.CreateFileSystemService(),
        subFileSystem,
        m_physicalConsole,
        m_allowStartProcess && permissions.allowStartProcess,
        m_pidCounter);
}

IFileSystem& HaisosOS::GetFileSystem() {
    return *m_rootFileSystem;
}

IFilesystemService& HaisosOS::GetFileSystemService() {
    return *m_filesystemServiceFactory;
}

IServicesCreator& HaisosOS::GetServicesCreator() {
    return m_servicesCreator;
}

std::shared_ptr<IHaisosOS> CreateHaisosOS(
    IServicesCreator& servicesCreator,
    std::shared_ptr<IFileSystem> rootFileSystem,
    std::shared_ptr<IPhysicalConsole> physicalConsole,
    const std::string& endpoint,
    const std::string& modelName,
    const std::string& apiKey)
{
    std::shared_ptr<INetworkService> networkService = servicesCreator.CreateNetworkService();
    std::shared_ptr<ILLMService> llmService = servicesCreator.CreateLLMService(*networkService, endpoint, modelName, apiKey);
    auto filesystemServiceFactory = servicesCreator.CreateFileSystemService();

    return std::make_shared<HaisosOS>(
        servicesCreator,
        std::move(networkService),
        std::move(llmService),
        std::move(filesystemServiceFactory),
        std::move(rootFileSystem),
        std::move(physicalConsole),
        /*allowStartProcess=*/true,
        std::make_shared<std::atomic<uint64_t>>(1));
}

}
