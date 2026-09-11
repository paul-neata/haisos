#pragma once

#include <string>
#include <utility>
#include <vector>

namespace Haisos {

struct HaisosFileRunEntry {
    std::string programPath;
    std::vector<std::string> args;
};

struct HaisosFileConfig {
    // Directory to mount as the OS's filesystem root. Empty means "use the
    // haisosfile's own directory" (the caller fills this default in, since
    // the parser only sees the file's content, not its path).
    std::string rootPath;
    std::vector<HaisosFileRunEntry> runEntries;
};

struct HaisosFileParseResult {
    HaisosFileConfig config;
    std::string error;
};

// Parses a haisosfile's content. argOverrides are "name"->"value" pairs (from
// the CLI's `-- name=value ...`) that override an ARG directive's default.
HaisosFileParseResult ParseHaisosFile(
    const std::string& content,
    const std::vector<std::pair<std::string, std::string>>& argOverrides);

} // namespace Haisos
