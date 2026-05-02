#pragma once
#include <memory>
#include <string>
#include <vector>
#include "IAgent.h"
#include "IConsole.h"
#include "IHTTPClient.h"
#include "ILLMCommunicator.h"
#include "IHaisosEngine.h"
#include "IFileSystem.h"
#include "IToolFactory.h"
#include "SystemCallbacks.h"

namespace Haisos {

class IFactory {
public:
    virtual ~IFactory() = default;
    virtual std::unique_ptr<IConsole> CreateConsole(bool registerAsLogMessageReceiver) = 0;
    virtual std::unique_ptr<IHTTPClient> CreateHTTPClient() = 0;
    virtual std::unique_ptr<ILLMCommunicator> CreateLLMCommunicator(
        std::unique_ptr<IHTTPClient> httpClient,
        const std::string& endpoint,
        const std::string& modelName,
        const std::string& apiKey) = 0;
    virtual std::unique_ptr<IToolFactory> CreateToolFactory(IFactory& factory) = 0;
    virtual std::shared_ptr<IAgent> CreateAgent(
        std::unique_ptr<ILLMCommunicator> llmCommunicator,
        std::unique_ptr<IToolFactory> toolFactory,
        std::unique_ptr<IConsole> console,
        const std::vector<std::string>& systemPrompts,
        const std::string& name,
        std::shared_ptr<IAgent> parent,
        const std::string& startTime = "",
        bool longRunning = true) = 0;
    virtual std::unique_ptr<IHaisosEngine> CreateHaisosEngine(IFactory& factory) = 0;
    virtual std::unique_ptr<IFileSystem> CreateFilesystem() = 0;

    virtual SystemCallbacks GetSystemCallbacks() const = 0;
    virtual void SetSystemCallbacks(const SystemCallbacks& callbacks) = 0;
};

std::unique_ptr<IFactory> CreateFactory();

}
