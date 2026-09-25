#include "HaisosFileSystemBuilder.h"
#include <unordered_map>
#include "src/components/Logger/Logger.h"

namespace Haisos {

namespace {

std::string JoinArgs(const std::vector<std::string>& args) {
    std::string joined;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            joined += ", ";
        }
        joined += args[i];
    }
    return joined;
}

} // namespace

std::shared_ptr<IFileSystem> BuildRootFileSystem(
    IFactory& factory,
    IFileSystemService& filesystemService,
    const HaisosFileConfig& config,
    const std::filesystem::path& haisosFileDir,
    std::string& outError,
    std::unordered_map<std::string, std::shared_ptr<IFileSystem>>* outNamedFileSystems)
{
    if (config.fsSteps.empty()) {
        std::string rootPath = config.rootPath.empty()
            ? haisosFileDir.string()
            : (haisosFileDir / config.rootPath).string();
        return factory.CreatePhysicalFileSystem(rootPath);
    }

    std::unordered_map<std::string, std::shared_ptr<IFileSystem>> namedFs;
    std::string lastDeclaredName;

    for (const auto& step : config.fsSteps) {
        if (!step.isMount) {
            const auto& decl = step.declare;
            LogDebug("HaisosFileSystemBuilder: FS %s %s %s", decl.name.c_str(), decl.type.c_str(), JoinArgs(decl.args).c_str());
            if (namedFs.find(decl.name) != namedFs.end()) {
                outError = "Error: Duplicate FS declaration: '" + decl.name + "' is already declared\n";
                return nullptr;
            }
            std::shared_ptr<IFileSystem> fs;
            if (decl.type == "PHYSICAL") {
                fs = factory.CreatePhysicalFileSystem((haisosFileDir / decl.args[0]).string());
            } else if (decl.type == "MEM") {
                fs = filesystemService.CreateEmptyInMemFileSystem();
            } else if (decl.type == "DEV") {
                fs = filesystemService.CreateDeviceFileSystem();
            } else if (decl.type == "RO") {
                auto it = namedFs.find(decl.args[0]);
                if (it == namedFs.end()) {
                    outError = "Error: FS " + decl.name + " RO references unknown filesystem '" + decl.args[0] + "'\n";
                    return nullptr;
                }
                fs = filesystemService.CreateReadOnlyFileSystem(it->second);
            } else if (decl.type == "SUB") {
                auto it = namedFs.find(decl.args[0]);
                if (it == namedFs.end()) {
                    outError = "Error: FS " + decl.name + " SUB references unknown filesystem '" + decl.args[0] + "'\n";
                    return nullptr;
                }
                fs = filesystemService.CreateSubFileSystem(it->second, decl.args[1]);
            } else if (decl.type == "COMPOSED") {
                // FS <name> COMPOSED <main> <path> <mounted>: a new filesystem,
                // leaving both operands untouched (unlike MOUNT, which mutates).
                auto mainIt = namedFs.find(decl.args[0]);
                auto mountedIt = namedFs.find(decl.args[2]);
                if (mainIt == namedFs.end()) {
                    outError = "Error: FS " + decl.name + " COMPOSED references unknown filesystem '" + decl.args[0] + "'\n";
                    return nullptr;
                }
                if (mountedIt == namedFs.end()) {
                    outError = "Error: FS " + decl.name + " COMPOSED references unknown filesystem '" + decl.args[2] + "'\n";
                    return nullptr;
                }
                fs = filesystemService.CreateComposedFileSystem(mainIt->second, decl.args[1], mountedIt->second);
            }
            if (!fs) {
                outError = "Error: failed to build filesystem '" + decl.name + "'\n";
                return nullptr;
            }
            namedFs[decl.name] = fs;
            lastDeclaredName = decl.name;
        } else {
            const auto& mount = step.mount;
            LogDebug("HaisosFileSystemBuilder: MOUNT %s %s %s", mount.mainFs.c_str(), mount.path.c_str(), mount.toBeMountedFs.c_str());
            auto mainIt = namedFs.find(mount.mainFs);
            auto mountedIt = namedFs.find(mount.toBeMountedFs);
            if (mainIt == namedFs.end()) {
                outError = "Error: MOUNT references unknown filesystem '" + mount.mainFs + "'\n";
                return nullptr;
            }
            if (mountedIt == namedFs.end()) {
                outError = "Error: MOUNT references unknown filesystem '" + mount.toBeMountedFs + "'\n";
                return nullptr;
            }
            namedFs[mount.mainFs] = filesystemService.CreateComposedFileSystem(mainIt->second, mount.path, mountedIt->second);
        }
    }

    if (outNamedFileSystems) {
        *outNamedFileSystems = namedFs;
    }

    std::string rootName = config.rootPath.empty() ? lastDeclaredName : config.rootPath;
    auto namedIt = namedFs.find(rootName);
    if (namedIt != namedFs.end()) {
        return namedIt->second;
    }
    if (!config.rootPath.empty()) {
        // ROOT didn't name a declared filesystem: legacy shorthand, treat it
        // as a plain directory path.
        LogWarning("HaisosFileSystemBuilder: ROOT '%s' does not match a declared FS; treating it as a plain directory path", config.rootPath.c_str());
        return factory.CreatePhysicalFileSystem((haisosFileDir / config.rootPath).string());
    }
    outError = "Error: could not determine a root filesystem\n";
    return nullptr;
}

} // namespace Haisos
