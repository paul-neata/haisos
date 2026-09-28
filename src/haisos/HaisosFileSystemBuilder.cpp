#include "HaisosFileSystemBuilder.h"
#include <system_error>
#include <unordered_map>
#include "src/components/Filesystem/PhysicalPath.h"
#include "src/components/Logger/Logger.h"

namespace Haisos {

namespace {

// FS <name> PHYSICAL <directory>, or a ROOT naming a directory: that directory
// of the host's full physical filesystem, as a filesystem of its own. It may be
// written in any form CreatePhysicalFileSystem takes -- /c/x, c:\x or c:/x on
// Windows -- and a relative one is taken from the haisosfile's directory. It
// is jailed at the directory (CreatePhysicalFileSystem), not a SubFileSystem of
// the full one: a symbolic link inside it then stays inside, where a
// SubFileSystem, confining paths only as written, would follow it anywhere on
// the disk. A directory that is not there is an error here, rather than a
// process failing to start later. |directive| names the line, for errors.
std::shared_ptr<IFileSystem> TakePhysicalDirectory(
    IFactory& factory,
    const std::filesystem::path& haisosFileDir,
    const std::string& directory,
    const std::string& directive,
    std::string& outError)
{
    std::shared_ptr<IFileSystem> fs;
    std::string where = directory;
    if (IsFullFileSystemRoot(directory)) {
        fs = factory.CreateFullPhysicalFileSystem();
    } else {
        std::error_code ec;
        const std::filesystem::path base = std::filesystem::absolute(haisosFileDir, ec);
        std::string reason;
        auto hostPath = ResolvePhysicalPath(directory, ec ? haisosFileDir.u8string() : base.u8string(), &reason);
        if (!hostPath) {
            outError = "Error: " + directive + ": " + reason + "\n";
            return nullptr;
        }
        // As written in the error; the host resolves ".." itself, after links.
        where = std::filesystem::u8path(*hostPath).lexically_normal().u8string();
        fs = factory.CreatePhysicalFileSystem(*hostPath);
    }
    FileStatus status;
    if (!fs || fs->Stat("/", status) != 0 || status.type != DirectoryEntryType::Dir) {
        outError = "Error: " + directive + ": " + where + " is not a directory\n";
        return nullptr;
    }
    return fs;
}

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
        // No FS at all: ROOT is a directory (the haisosfile's own, if omitted).
        const std::string directory = config.rootPath.empty() ? "." : config.rootPath;
        return TakePhysicalDirectory(factory, haisosFileDir, directory, "ROOT " + directory, outError);
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
                fs = TakePhysicalDirectory(factory, haisosFileDir, decl.args[0], "FS " + decl.name + " PHYSICAL " + decl.args[0], outError);
                if (!fs) {
                    return nullptr;
                }
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
    // Once any FS is declared, ROOT names one of them: a directory path there
    // is most likely a mistyped name, and taken as a path it would boot an OS
    // on the wrong files, or on none, with nothing pointing at ROOT.
    std::string declared;
    for (const auto& step : config.fsSteps) {
        if (!step.isMount) {
            declared += (declared.empty() ? "" : ", ") + step.declare.name;
        }
    }
    outError = "Error: ROOT " + rootName + ": no filesystem of that name is declared with FS (declared: " + declared + ")\n";
    return nullptr;
}

} // namespace Haisos
