#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "ILLMService.h"
#include "INetworkService.h"
#include "ILLMCommunicator.h"
#include "IFileSystemService.h"
#include "IEnvironment.h"
#include "IPhysicalConsole.h"
#include "IServicesCreator.h"
#include "IHaisosOS.h"

namespace Haisos {



// IFactory creates the root concepts -- the things that have to exist before
// anything else can: a physical console, a disk-backed filesystem, an
// environment, the services layer, and the OS itself. (HTTP clients come from
// INetworkService.) It deliberately knows
// nothing about agents, LLM communication, or tool factories; that all lives in
// ILLMService (see IServicesCreator), which is what the rest of the platform
// should generally depend on.
//
// Note there is no way to obtain an unrooted filesystem here. Every filesystem
// handed out is anchored somewhere, and an OS is given its root at creation and
// can never step outside it: it can only compose further filesystems *on top of* that
// root via IFileSystemService.
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
    virtual std::shared_ptr<IFileSystem> CreatePhysicalFileSystem(const std::string& rootPath) = 0;

    // A new, empty environment, to be filled in and then handed to an OS, a
    // process or a sub-OS. An existing one is duplicated with
    // IEnvironment::Clone() rather than rebuilt here.
    virtual std::shared_ptr<IEnvironment> CreateEnvironment() = 0;

    virtual std::shared_ptr<IServicesCreator> CreateServicesCreator() = 0;

    // Builds an IHaisosOS around an already-built root filesystem, which becomes
    // the OS's root for its whole life. environment becomes the OS's own
    // environment, and is where the LLM endpoint/model/API key are read from.
    // Typically -- but not automatically -- it is what its processes and sub-OS
    // instances go on to run with: each of those is passed an environment
    // explicitly (see IHaisosOS::StartProcess and IHaisosOS::CreateSubOS),
    // usually a Clone() of this one.
    // The new OS is given a fresh pid from GetNextGloballyUniquePID(): every OS
    // created here is a root of its own tree, so it borrows no process's
    // identity (see IHaisosOS::CreateSubOS for the sub-OS case, which does).
    virtual std::shared_ptr<IHaisosOS> CreateHaisosOS(
        std::shared_ptr<IServicesCreator> servicesCreator,
        std::shared_ptr<IPhysicalConsole> physicalConsole,
        std::shared_ptr<IFileSystem> rootFileSystem,
        std::shared_ptr<IEnvironment> environment) = 0;

    // Allocates a process id that is unique across every OS in this program: a
    // pid that exists in one OS can never turn up in another, so a pid
    // identifies a process (and, through IHaisosOS::GetOSProcessID(), the OS it
    // runs under) on its own. The counter is the program's, not this factory's,
    // so two factories still never hand out the same id. 0 is never allocated:
    // it means "no parent".
    virtual uint64_t GetNextGloballyUniquePID() = 0;
};

std::shared_ptr<IFactory> CreateFactory();

}
