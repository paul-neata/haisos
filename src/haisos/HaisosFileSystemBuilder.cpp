#include "HaisosFileSystemBuilder.h"
#include <unordered_map>

namespace Haisos {

std::shared_ptr<IFileSystem> BuildRootFileSystem(
    IFactory& factory,
    IFilesystemService& filesystemService,
    const HaisosFileConfig& config,
    const std::filesystem::path& haisosFileDir,
    std::string& outError)
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
            std::shared_ptr<IFileSystem> fs;
            if (decl.type == "PHYSICAL") {
                fs = factory.CreatePhysicalFileSystem((haisosFileDir / decl.args[0]).string());
            } else if (decl.type == "MEM") {
                fs = filesystemService.CreateEmptyInMemFileSystem();
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
            }
            if (!fs) {
                outError = "Error: failed to build filesystem '" + decl.name + "'\n";
                return nullptr;
            }
            namedFs[decl.name] = fs;
            lastDeclaredName = decl.name;
        } else {
            const auto& mount = step.mount;
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
            namedFs[mount.mainFs] = filesystemService.MountFileSystem(mainIt->second, mount.path, mountedIt->second);
        }
    }

    std::string rootName = config.rootPath.empty() ? lastDeclaredName : config.rootPath;
    auto namedIt = namedFs.find(rootName);
    if (namedIt != namedFs.end()) {
        return namedIt->second;
    }
    if (!config.rootPath.empty()) {
        // ROOT didn't name a declared filesystem: legacy shorthand, treat it
        // as a plain directory path.
        return factory.CreatePhysicalFileSystem((haisosFileDir / config.rootPath).string());
    }
    outError = "Error: could not determine a root filesystem\n";
    return nullptr;
}

} // namespace Haisos
