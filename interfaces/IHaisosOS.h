#pragma once
#include <memory>
#include <string>
#include <vector>
#include "IProcess.h"
#include "IServicesCreator.h"

namespace Haisos {

// Permissions for a sub-OS created via IHaisosOS::CreateSubOS. This is
// deliberately scoped to logical/application-level confinement for now:
// a real filesystem sub-root and a coarse process-start toggle. Deeper
// permissioning (network access, composed filesystems, ...) is future work.
struct SubOSPermissions {
    // Directory (relative to the parent OS's root) the sub-OS is confined to.
    // Empty means "same root as the parent" (i.e. no extra filesystem confinement).
    std::string subRootRelativePath;
    bool allowStartProcess = true;
};

// An instance of an operating system: owns a rooted filesystem, a physical
// console, and a services layer, and can start processes (agent- or
// script-backed) and spawn more tightly-scoped sub-OS instances.
class IHaisosOS {
public:
    virtual ~IHaisosOS() = default;

    // programPath is resolved against this OS's filesystem root. The runtime is
    // selected by extension: ".md" starts an LLM agent, ".lua" starts a Lua
    // script (future: .js, .wasm, a real-OS-process driver, ...).
    // callerAgent identifies the calling agent (for parent/child bookkeeping);
    // pass nullptr for a top-most process (its GetParentPid() will be 0).
    virtual std::shared_ptr<IProcess> StartProcess(
        const std::string& programPath,
        const std::vector<std::string>& args,
        std::shared_ptr<IAgent> callerAgent) = 0;

    virtual std::vector<std::shared_ptr<IProcess>> GetRunningProcesses() const = 0;

    virtual std::shared_ptr<IHaisosOS> CreateSubOS(const SubOSPermissions& permissions) = 0;

    virtual IFilesystemService& GetFileSystemService() = 0;
    virtual IServicesCreator& GetServicesCreator() = 0;
};

// rootPath is the real disk directory filesystemService's IFileSystem is rooted
// at (used to resolve CreateSubOS's sub-roots against it).
std::shared_ptr<IHaisosOS> CreateHaisosOS(
    IFactory& factory,
    IServicesCreator& servicesCreator,
    std::shared_ptr<INetworkService> networkService,
    std::shared_ptr<ILLMService> llmService,
    std::unique_ptr<IFilesystemService> filesystemService,
    const std::string& rootPath,
    std::shared_ptr<IPhysicalConsole> physicalConsole,
    bool allowStartProcess = true);

}
