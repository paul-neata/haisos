#pragma once
#include <memory>
#include <string>
#include <vector>
#include "IProcess.h"
#include "IServicesCreator.h"

namespace Haisos {

// Permissions for a sub-OS created via IHaisosOS::CreateSubOS. This is
// deliberately scoped to logical/application-level confinement for now:
// a filesystem sub-root and a coarse process-start toggle. Deeper
// permissioning (network access, ...) is future work.
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

    // The OS's own mounted root filesystem.
    virtual IFileSystem& GetFileSystem() = 0;
    // The filesystem-composition factory (read-only/in-memory/sub/mount views).
    virtual IFilesystemService& GetFileSystemService() = 0;
    virtual IServicesCreator& GetServicesCreator() = 0;
};

// Builds the root IHaisosOS. networkService/llmService/filesystem-service are
// not passed in -- they are created internally via servicesCreator (llmService
// using endpoint/modelName/apiKey); rootFileSystem becomes the OS's initial
// mounted root (build one via IFactory::CreatePhysicalFileSystem for a real
// disk directory, or via a filesystem service's CreateEmptyInMemFileSystem/
// etc. for anything else). The root OS always allows starting processes;
// only a sub-OS (see CreateSubOS) can be more restricted.
std::shared_ptr<IHaisosOS> CreateHaisosOS(
    IServicesCreator& servicesCreator,
    std::shared_ptr<IFileSystem> rootFileSystem,
    std::shared_ptr<IPhysicalConsole> physicalConsole,
    const std::string& endpoint,
    const std::string& modelName,
    const std::string& apiKey);

}
