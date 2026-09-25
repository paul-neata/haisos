#pragma once
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace Haisos {

// Lexical (no real disk access) POSIX-style path normalization, for
// filesystem implementations that have no real disk path to canonicalize
// against (in-memory, sub, mounted). Resolves "." and ".." segments and
// always returns an absolute path starting with '/' (a leading ".." is
// dropped rather than escaping, matching how "cd .." at "/" is a no-op).
inline std::string NormalizeVirtualPath(const std::string& path, const std::string& base = "/") {
    std::string joined = (!path.empty() && path[0] == '/') ? path : (base + "/" + path);

    std::vector<std::string> segments;
    std::istringstream iss(joined);
    std::string segment;
    while (std::getline(iss, segment, '/')) {
        if (segment.empty() || segment == ".") {
            continue;
        }
        if (segment == "..") {
            if (!segments.empty()) {
                segments.pop_back();
            }
            continue;
        }
        segments.push_back(segment);
    }

    if (segments.empty()) {
        return "/";
    }
    std::string result;
    for (const auto& s : segments) {
        result += "/" + s;
    }
    return result;
}

// If |normalizedPath| is |normalizedBase| itself or strictly under it,
// returns the remainder as an absolute path (relative to base's own root);
// otherwise returns std::nullopt. Both inputs must already be normalized
// (see NormalizeVirtualPath).
inline std::optional<std::string> RelativeToBase(const std::string& normalizedBase, const std::string& normalizedPath) {
    if (normalizedPath == normalizedBase) {
        return std::string("/");
    }
    std::string prefix = (normalizedBase == "/") ? "/" : (normalizedBase + "/");
    if (normalizedPath.compare(0, prefix.size(), prefix) == 0) {
        return normalizedPath.substr(prefix.size() - 1);
    }
    return std::nullopt;
}

// The directory holding |normalizedPath| ("/" for the root and its children).
inline std::string VirtualParentOf(const std::string& normalizedPath) {
    auto pos = normalizedPath.find_last_of('/');
    return (pos == std::string::npos || pos == 0) ? "/" : normalizedPath.substr(0, pos);
}

// The last segment of |normalizedPath| (empty for the root).
inline std::string VirtualLastSegment(const std::string& normalizedPath) {
    auto pos = normalizedPath.find_last_of('/');
    return (pos == std::string::npos) ? normalizedPath : normalizedPath.substr(pos + 1);
}

} // namespace Haisos
