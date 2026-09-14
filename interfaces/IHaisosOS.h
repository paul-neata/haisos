#pragma once
#include <memory>
#include <string>
#include <vector>
#include "IProcess.h"
#include "IServicesCreator.h"
#include "IOSEnvironment.h"

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

    // creatorProcessPid is the pid of the process spawning the sub-OS; it becomes
    // the sub-OS's GetOSProcessID(). The sub-OS inherits this OS's environment.
    virtual std::shared_ptr<IHaisosOS> CreateSubOS(
        const SubOSPermissions& permissions,
        uint64_t creatorProcessPid) = 0;

    // The OS's root filesystem, fixed at creation. An OS cannot step outside it;
    // GetFileSystemService() below composes further filesystems on top of it.
    virtual IFileSystem& GetRootFileSystem() = 0;
    // The filesystem-composition factory (read-only/in-memory/sub/mount).
    virtual IFilesystemService& GetFileSystemService() = 0;
    // Access to this OS's own (sandboxed) services.
    virtual IServicesCreator& GetServicesCreator() = 0;

    // The environment this OS was created with, inherited by every process and
    // sub-OS it starts.
    virtual const OSEnvironment& GetOsEnvironment() const = 0;

    // 0 for the initial OS; for a sub-OS, the pid of the process that created it.
    virtual uint64_t GetOSProcessID() const = 0;
};

// Allocates a process id that is unique across every OS in this program.
uint64_t AllocateUniquePid();

}
