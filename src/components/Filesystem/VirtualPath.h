#pragma once
#include <optional>
#include <string>
#include <vector>

namespace Haisos {

// Whether |c| separates the segments of a path: '/' everywhere, and '\' as well
// on Windows. The host takes either there, and a filesystem must agree with the
// host on where a path's segments are: were "a\b" one name to a mount and two
// to the disk, "..\x" would slip past a sub-path's confinement and "mnt\x"
// past a mount. Elsewhere '\' is part of a name, as it is to the host. Either
// way, every path Haisos hands back is separated by '/'.
inline bool IsVirtualPathSeparator(char c) {
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

// Splits |path| at its separators onto |segments|: empty segments and "." are
// dropped, and ".." takes away the segment before it. Returns false if a ".."
// finds none to take away -- it climbs above where |segments| started -- and
// leaves that ".." out, so the result is what "cd .." at the root gives.
inline bool AppendVirtualPathSegments(const std::string& path, std::vector<std::string>& segments) {
    bool stayedWithin = true;
    std::string segment;
    auto endSegment = [&]() {
        if (segment == "..") {
            if (segments.empty()) {
                stayedWithin = false;
            } else {
                segments.pop_back();
            }
        } else if (!segment.empty() && segment != ".") {
            segments.push_back(segment);
        }
        segment.clear();
    };
    for (char c : path) {
        if (IsVirtualPathSeparator(c)) {
            endSegment();
        } else {
            segment += c;
        }
    }
    endSegment();
    return stayedWithin;
}

// The segments of |path|, taken from the root ("foo" and "/foo" alike), "."
// and ".." resolved. Returns false if a ".." would climb above the root, which
// NormalizeVirtualPath quietly stays at instead -- for a filesystem that
// refuses such a path rather than read it from the root.
inline bool SplitVirtualPath(const std::string& path, std::vector<std::string>& segments) {
    segments.clear();
    return AppendVirtualPathSegments(path, segments);
}

// Lexical (no real disk access) POSIX-style path normalization, for
// filesystem implementations that have no real disk path to canonicalize
// against (in-memory, sub, mounted). Resolves "." and ".." segments and
// always returns an absolute path starting with '/', separated by '/' alone (a
// leading ".." is dropped rather than escaping, matching how "cd .." at "/" is
// a no-op). A relative |path| is taken from |base|.
inline std::string NormalizeVirtualPath(const std::string& path, const std::string& base = "/") {
    std::vector<std::string> segments;
    if (path.empty() || !IsVirtualPathSeparator(path[0])) {
        AppendVirtualPathSegments(base, segments);
    }
    AppendVirtualPathSegments(path, segments);

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
