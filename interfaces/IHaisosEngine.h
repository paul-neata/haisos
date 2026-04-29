#pragma once
#include <memory>
#include <string>
#include "SystemCallbacks.h"

namespace Haisos {

struct RunConfig {
    std::string userPrompt;
    std::string systemPrompt;
    bool useFile = false;
};

class IHaisosEngine {
public:
    virtual ~IHaisosEngine() = default;
    virtual void Run(const RunConfig& config, const SystemCallbacks& callbacks = {}) = 0;
};

}
