#pragma once
#include <memory>
#include <string>
#include "interfaces/IBuiltinCommands.h"

namespace Haisos {

// The IBuiltinConfigurator: checks a placement against the rules (see
// IBuiltinConfigurator), then hands it to the filesystem. Holds no state.
class BuiltinConfigurator : public IBuiltinConfigurator {
public:
    static std::shared_ptr<BuiltinConfigurator> Create();
    ~BuiltinConfigurator() override;

    bool AddBuiltinCommand(
        std::shared_ptr<IFileSystem> filesystem,
        const std::string& builtinPath,
        const std::string& builtinName,
        std::string* outError = nullptr) override;
    bool RemoveBuiltin(std::shared_ptr<IFileSystem> filesystem, const std::string& builtinPath) override;

private:
    BuiltinConfigurator();
};

} // namespace Haisos
