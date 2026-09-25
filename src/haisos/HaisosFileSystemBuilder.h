#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include "interfaces/IFactory.h"
#include "interfaces/IServicesCreator.h"
#include "HaisosFileParser.h"

namespace Haisos {

// Builds the OS's root filesystem, resolving FS/MOUNT directives (if any) and
// falling back to the legacy "ROOT is a plain directory path" shorthand when
// the haisosfile declares no filesystems at all. Returns nullptr and fills
// outError on any failure. haisosFileDir is the base every relative FS
// PHYSICAL path (and a legacy plain-path ROOT) is resolved against.
//
// outNamedFileSystems, when given, receives every declared FS by name, as it
// stands once all FS/MOUNT directives are applied -- what a BUILTIN directive
// names. A haisosfile declaring no FS has no names, so it stays empty.
std::shared_ptr<IFileSystem> BuildRootFileSystem(
    IFactory& factory,
    IFileSystemService& filesystemService,
    const HaisosFileConfig& config,
    const std::filesystem::path& haisosFileDir,
    std::string& outError,
    std::unordered_map<std::string, std::shared_ptr<IFileSystem>>* outNamedFileSystems = nullptr);

} // namespace Haisos
