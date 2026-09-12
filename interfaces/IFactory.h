#pragma once
#include <memory>
#include <string>
#include <vector>
#include "ILLMService.h"
#include "INetworkService.h"
#include "ILLMCommunicator.h"
#include "IFilesystemService.h"

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

// IFactory is the low-level factory for real/physical primitives: a physical
// console, an HTTP client, and disk-backed filesystems. It deliberately knows
// nothing about agents, LLM communication, or tool factories any more -- that
// all lives in ILLMService now (see IServicesCreator), which is what the rest
// of the platform should generally depend on.
class IFactory {
public:
    virtual ~IFactory() = default;

    virtual std::shared_ptr<IPhysicalConsole> CreatePhysicalConsole(bool registerAsLogMessageReceiver) = 0;
    virtual std::unique_ptr<IHTTPClient> CreateHTTPClient() = 0;

    virtual std::unique_ptr<IFileSystem> CreateFilesystem() = 0;
    // Returns a filesystem jailed to (rooted at) a real disk path: every path
    // passed to it is resolved and validated against rootPath before delegating.
    virtual std::unique_ptr<IFileSystem> CreatePhysicalFileSystem(const std::string& rootPath) = 0;
};

std::unique_ptr<IFactory> CreateFactory();

}
