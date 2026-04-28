#pragma once
#include <string>
#include <mutex>
#include <sstream>
#include "interfaces/IVirtualConsole.h"

namespace Haisos {

class VirtualConsole : public IVirtualConsole {
public:
    VirtualConsole() = default;
    ~VirtualConsole() override = default;

    VirtualConsole(const VirtualConsole&) = delete;
    VirtualConsole& operator=(const VirtualConsole&) = delete;

    void Write(const std::string& message) override;
    void Write(const IAgent& agent, const std::string& message) override;

    void Start() override {}
    void Stop() override {}

    std::string GetContents() const override;
    void Clear() override;

private:
    mutable std::mutex m_mutex;
    std::stringstream m_buffer;
};

}
