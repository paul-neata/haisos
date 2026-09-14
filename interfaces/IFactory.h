#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "ILLMService.h"
#include "INetworkService.h"
#include "ILLMCommunicator.h"
#include "IFilesystemService.h"
#include "IOSEnvironment.h"
#include "IServicesCreator.h"
#include "IHaisosOS.h"

namespace Haisos {


// The real, physical console (stdout, shared across whatever writes to it).
// Distinct from IAgentConsole, which is the minimal per-agent view onto it (or
// onto nothing at all, for an in-memory-only console).
class IPhysicalConsole {
public:
    virtual ~IPhysicalConsole() = default;
    virtual void Write(const std::string& message) = 0;
    virtual void Write(const std::string& sourceName, const std::string& message) = 0;
    virtual void Start() = 0;
    virtual void Stop() = 0;
};

// IFactory creates the root concepts -- the things that have to exist before
// anything else can: a physical console, a disk-backed filesystem, the
// services layer, and the OS itself. (HTTP clients come from INetworkService.) It deliberately knows
// nothing about agents, LLM communication, or tool factories; that all lives in
// ILLMService (see IServicesCreator), which is what the rest of the platform
// should generally depend on.
//
// Note there is no way to obtain an unrooted filesystem here. Every filesystem
// handed out is anchored somewhere, and an OS is given its root at creation and
// can never step outside it: it can only compose further filesystems *on top of* that
// root via IFilesystemService.
class IFactory {
public:
    virtual ~IFactory() = default;

    virtual std::shared_ptr<IPhysicalConsole> CreatePhysicalConsole(bool registerAsLogMessageReceiver) = 0;

    // Returns a filesystem jailed to (rooted at) a real disk path: every path
    // passed to it is resolved and validated against rootPath before delegating.
    // Writing through it writes straight to disk, which is the point -- mounting
    // one inside another filesystem (an in-memory one, say) keeps that property
    // for the paths it covers, because each call is routed to whichever
    // filesystem owns the path and that filesystem decides what the call means.
    virtual std::unique_ptr<IFileSystem> CreatePhysicalFileSystem(const std::string& rootPath) = 0;

    virtual std::unique_ptr<IServicesCreator> CreateServicesCreator() = 0;

    // Builds an IHaisosOS around an already-built root filesystem, which becomes
    // the OS's root for its whole life. environment is the OS's initial
    // environment, inherited by every process and sub-OS it goes on to create,
    // and is where the LLM endpoint/model/API key are read from.
    // osProcessId identifies the process this OS belongs to: 0 for the initial
    // OS, and the pid of the creating process for a sub-OS. Process ids come
    // from a single global allocator, so these are unique across every OS.
    virtual std::shared_ptr<IHaisosOS> CreateHaisosOS(
        std::shared_ptr<IFileSystem> rootFileSystem,
        IServicesCreator& servicesCreator,
        std::shared_ptr<IPhysicalConsole> physicalConsole,
        const OSEnvironment& environment,
        uint64_t osProcessId) = 0;
};

std::unique_ptr<IFactory> CreateFactory();

}
