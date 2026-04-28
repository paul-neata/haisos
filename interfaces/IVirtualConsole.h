#pragma once
#include <string>
#include "interfaces/IConsole.h"

namespace Haisos {

class IVirtualConsole : public IConsole {
public:
    virtual ~IVirtualConsole() = default;
    virtual std::string GetContents() const = 0;
    virtual void Clear() = 0;
};

}
