#pragma once
#include <string>

namespace Haisos {
    class IAgent;

    class IConsole {
    public:
        virtual ~IConsole() = default;
        virtual void Write(const std::string& message) = 0;
        virtual void Write(const IAgent& agent, const std::string& message) = 0;
        virtual void Start() = 0;
        virtual void Stop() = 0;
    };
}
