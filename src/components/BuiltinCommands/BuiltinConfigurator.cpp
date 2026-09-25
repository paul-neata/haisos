#include "BuiltinConfigurator.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/Filesystem/VirtualPath.h"
#include "src/components/Logger/Logger.h"

namespace Haisos {

namespace {

bool Fail(std::string* outError, const std::string& message) {
    LogWarning("BuiltinConfigurator: %s", message.c_str());
    if (outError) {
        *outError = message;
    }
    return false;
}

} // namespace

std::shared_ptr<BuiltinConfigurator> BuiltinConfigurator::Create() {
    return std::shared_ptr<BuiltinConfigurator>(new BuiltinConfigurator());
}

BuiltinConfigurator::BuiltinConfigurator() = default;
BuiltinConfigurator::~BuiltinConfigurator() = default;

bool BuiltinConfigurator::AddBuiltinCommand(
    std::shared_ptr<IFileSystem> filesystem,
    const std::string& builtinPath,
    const std::string& builtinName,
    std::string* outError)
{
    if (!filesystem) {
        return Fail(outError, "no filesystem to place builtin '" + builtinName + "' on");
    }
    if (builtinName.empty()) {
        return Fail(outError, "a builtin needs a name");
    }
    if (builtinPath.empty() || builtinPath[0] != '/') {
        return Fail(outError, "the path of builtin '" + builtinName + "' must be absolute, got '" + builtinPath + "'");
    }
    const std::string path = NormalizeVirtualPath(builtinPath);
    if (path == "/") {
        return Fail(outError, "a builtin cannot be placed at the root directory itself");
    }

    const std::string directory = VirtualParentOf(path);
    auto directoryType = EntryTypeOf(*filesystem, directory);
    if (!directoryType) {
        return Fail(outError, "cannot place builtin '" + builtinName + "' at " + path + ": the directory " + directory + " does not exist");
    }
    if (*directoryType != DirectoryEntryType::Dir) {
        return Fail(outError, "cannot place builtin '" + builtinName + "' at " + path + ": " + directory + " is not a directory");
    }
    if (auto existing = filesystem->IsBuiltinCommand(path)) {
        return Fail(outError, "cannot place builtin '" + builtinName + "' at " + path + ": the builtin '" + *existing + "' is already there");
    }
    if (EntryTypeOf(*filesystem, path)) {
        return Fail(outError, "cannot place builtin '" + builtinName + "' at " + path + ": something already exists there");
    }
    if (filesystem->AddBuiltinCommand(path, builtinName) != 0) {
        return Fail(outError, "cannot place builtin '" + builtinName + "' at " + path + ": the filesystem refused it");
    }
    LogDebug("BuiltinConfigurator: placed builtin '%s' at %s", builtinName.c_str(), path.c_str());
    return true;
}

bool BuiltinConfigurator::RemoveBuiltin(std::shared_ptr<IFileSystem> filesystem, const std::string& builtinPath) {
    if (!filesystem) {
        return false;
    }
    const std::string path = NormalizeVirtualPath(builtinPath);
    if (filesystem->RemoveBuiltinCommand(path) != 0) {
        LogDebug("BuiltinConfigurator: no builtin of this filesystem's own at %s to remove", path.c_str());
        return false;
    }
    LogDebug("BuiltinConfigurator: removed the builtin at %s", path.c_str());
    return true;
}

} // namespace Haisos
