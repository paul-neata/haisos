#pragma once
#include <memory>
#include <string>
#include "interfaces/IHaisosEngine.h"
#include "interfaces/IFactory.h"
#include "src/components/Logger/Logger.h"

namespace Haisos {

class HaisosEngine : public IHaisosEngine {
public:
    explicit HaisosEngine(IFactory& factory);
    ~HaisosEngine() override;

    // IHaisosEngine interface
    void Run(const RunConfig& config, const JsonSendReceiveCallbacks& callbacks = {}) override;

private:
    std::string ReadFile(const std::string& filePath);

    IFactory& m_factory;
};

}
