#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "IProcess.h"
#include "IServicesCreator.h"
#include "IEnvironment.h"
#include "IPhysicalConsole.h"

namespace Haisos {

// An instance of an operating system: owns a rooted filesystem, a physical
// console, and a services layer, and can start processes (agent- or
// script-backed) and spawn more tightly-scoped sub-OS instances.
//
// A running process reaches its OS only through ICurrentProcess::GetHaisosOS(),
// never through an IHaisosOS handed to it directly -- that is what lets one
// process be given a narrower OS than another without any runtime knowing.
class IHaisosOS {
public:
    virtual ~IHaisosOS() = default;

    // environment is what the new process runs with. It is not taken from the
    // OS behind the caller's back: pass GetOsEnvironment()->Clone() to hand on
    // this OS's own, or a narrowed one to give the process less.
    // programPath is resolved against this OS's filesystem root. The runtime is
    // selected by extension: ".md" starts an LLM agent, ".lua" starts a Lua
    // script (future: .js, .wasm, a real-OS-process driver, ...). Which of
    // those it is is not the caller's business: a process is opaque, and an
    // agent-backed one that starts subagents keeps them inside itself rather
    // than turning them into processes.
    // workingDirectory is where the new process starts out, and is likewise
    // passed rather than inherited: a filesystem has no current directory of
    // its own, so this is the only thing a relative path is resolved against
    // (see ICurrentProcess::ChangeDirectory). Empty means the OS's root.
    virtual std::shared_ptr<IProcess> StartProcess(
        std::shared_ptr<IEnvironment> environment,
        const std::string& programPath,
        const std::vector<std::string>& args,
        const std::string& workingDirectory) = 0;

    virtual std::vector<std::shared_ptr<IProcess>> GetRunningProcesses() const = 0;

    // Same shape as IFactory::CreateHaisosOS: a sub-OS is an OS like any other,
    // and it is confined by the root filesystem it is handed rather than by a
    // permission flag. It is likewise given its environment explicitly --
    // typically GetOsEnvironment()->Clone(). It carries this OS's process id
    // rather than a fresh one: a sub-OS is a narrowing of the same OS, not a
    // new one belonging to some other process.
    virtual std::shared_ptr<IHaisosOS> CreateSubOS(
        std::shared_ptr<IServicesCreator> servicesCreator,
        std::shared_ptr<IPhysicalConsole> physicalConsole,
        std::shared_ptr<IFileSystem> rootFileSystem,
        std::shared_ptr<IEnvironment> environment) = 0;

    // The OS's root filesystem, fixed at creation. An OS cannot step outside
    // it, but it can compose further filesystems on top of it -- narrowing this
    // one with IFileSystemService::CreateSubFileSystem is how a sub-OS gets a
    // root inside this one.
    virtual std::shared_ptr<IFileSystem> GetRootFileSystem() = 0;

    // This OS's own (sandboxed) services: the filesystem-composition service
    // and every other service comes from here.
    virtual std::shared_ptr<IServicesCreator> GetServicesCreator() = 0;

    // The environment this OS was created with. Clone() it before handing it to
    // a process or a sub-OS, so their edits do not reach back into this one.
    virtual std::shared_ptr<IEnvironment> GetOsEnvironment() const = 0;

    // The pid identifying this OS, allocated when it was created (see
    // IFactory::GetNextGloballyUniquePID). Every sub-OS created from it shares
    // the same id.
    virtual uint64_t GetOSProcessID() const = 0;
};

}
