#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include "interfaces/IServicesCreator.h"
#include "HaisosFileParser.h"

namespace Haisos {

// Builds the OS's root filesystem, resolving FS/MOUNT directives (if any) and
// falling back to the legacy "ROOT is a plain directory path" shorthand when
// the haisosfile declares no filesystems at all. Returns nullptr and fills
// outError on any failure. haisosFileDir is the base every relative FS
// PHYSICAL path (and a legacy plain-path ROOT) is resolved against.
std::shared_ptr<IFileSystem> BuildRootFileSystem(
    IFactory& factory,
    IFilesystemService& filesystemService,
    const HaisosFileConfig& config,
    const std::filesystem::path& haisosFileDir,
    std::string& outError);

} // namespace Haisos
