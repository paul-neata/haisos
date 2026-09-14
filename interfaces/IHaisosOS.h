#pragma once
#include <memory>
#include <string>
#include <vector>
#include "IProcess.h"
#include "IServicesCreator.h"
#include "IOSEnvironment.h"
#include "IPhysicalConsole.h"

namespace Haisos {

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

    // Same shape as IFactory::CreateHaisosOS: a sub-OS is an OS like any other,
    // and it is confined by the root filesystem it is handed rather than by a
    // permission flag. osProcessId is the pid of the process spawning it.
    virtual std::shared_ptr<IHaisosOS> CreateSubOS(
        std::shared_ptr<IServicesCreator> servicesCreator,
        std::shared_ptr<IPhysicalConsole> physicalConsole,
        std::shared_ptr<IFileSystem> rootFileSystem,
        const OSEnvironment& environment,
        uint64_t osProcessId) = 0;

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
