#include "PhysicalPath.h"
#include <cstring>
#include <vector>

namespace Haisos {

namespace {

bool IsWindowsSeparator(char c) {
    return c == '/' || c == '\\';
}

bool IsAsciiLetter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

char ToUpperAscii(char c) {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

char ToLowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

void SetError(std::string* error, const std::string& text) {
    if (error != nullptr) {
        *error = text;
    }
}

// Splits |text| at every separator onto |segments|: empty segments and "."
// are dropped, and ".." takes away the segment before it -- never one before
// the start, which is how Windows resolves a ".." at a drive's root or a
// share's.
void AppendWindowsSegments(const std::string& text, std::vector<std::string>& segments) {
    std::string segment;
    auto endSegment = [&]() {
        if (segment == "..") {
            if (!segments.empty()) {
                segments.pop_back();
            }
        } else if (!segment.empty() && segment != ".") {
            segments.push_back(segment);
        }
        segment.clear();
    };
    for (char c : text) {
        if (IsWindowsSeparator(c)) {
            endSegment();
        } else {
            segment += c;
        }
    }
    endSegment();
}

// An absolute Windows host path: a drive's root ("C:\") or a share
// ("\\server\share\"), and the segments below it.
struct WindowsPath {
    std::string root;
    std::vector<std::string> segments;

    std::string ToString() const {
        std::string path = root;
        for (size_t i = 0; i < segments.size(); ++i) {
            if (i > 0) {
                path += '\\';
            }
            path += segments[i];
        }
        return path;
    }
};

enum class WindowsPathKind { Absolute, Relative, Invalid };

// A UNC path: |path| starts with two separators, which the server's name follows.
WindowsPathKind ParseUncPath(const std::string& path, WindowsPath& out, std::string* error) {
    if (path.size() > 2 && IsWindowsSeparator(path[2])) {
        SetError(error, "'" + path + "' starts with three separators: a UNC path starts with two (\\\\server\\share)");
        return WindowsPathKind::Invalid;
    }
    // The server and the share are the first two segments, taken as they
    // are: a ".." cannot climb above the share, and never names one.
    size_t serverEnd = path.find_first_of("/\\", 2);
    const std::string server = path.substr(2, serverEnd == std::string::npos ? std::string::npos : serverEnd - 2);
    if (server == "?" || server == ".") {
        SetError(error, "'" + path + "' is a \\\\" + server + "\\ path, which is not taken: write the path it names without that prefix");
        return WindowsPathKind::Invalid;
    }
    size_t shareStart = (serverEnd == std::string::npos) ? std::string::npos : path.find_first_not_of("/\\", serverEnd);
    size_t shareEnd = (shareStart == std::string::npos) ? std::string::npos : path.find_first_of("/\\", shareStart);
    const std::string share = (shareStart == std::string::npos) ? std::string()
        : path.substr(shareStart, shareEnd == std::string::npos ? std::string::npos : shareEnd - shareStart);
    if (server.empty() || share.empty() || server == ".." || share == "." || share == "..") {
        SetError(error, "'" + path + "' is not a UNC path: one names a server and a share on it (\\\\server\\share)");
        return WindowsPathKind::Invalid;
    }
    out.root = "\\\\" + server + "\\" + share + "\\";
    out.segments.clear();
    if (shareEnd != std::string::npos) {
        AppendWindowsSegments(path.substr(shareEnd), out.segments);
    }
    return WindowsPathKind::Absolute;
}

// A path of the full physical filesystem: |path| starts with one separator,
// and its first segment is the drive.
WindowsPathKind ParseFullPath(const std::string& path, WindowsPath& out, std::string* error) {
    std::vector<std::string> segments;
    AppendWindowsSegments(path, segments);
    if (segments.empty()) {
        SetError(error, "'" + path + "' is the root of the full physical filesystem, which holds the drives but lies on none of them");
        return WindowsPathKind::Invalid;
    }
    if (!IsDriveLetter(segments[0])) {
        SetError(error, "'" + path + "' starts with '/', so it is a path of the full physical filesystem, whose top holds "
            "nothing but the drives (/c, /d, ...): '" + segments[0] + "' is no drive letter. A path on the current drive is "
            "written with its letter, as c:" + path + " or /c" + path);
        return WindowsPathKind::Invalid;
    }
    out.root = std::string(1, ToUpperAscii(segments[0][0])) + ":\\";
    out.segments.assign(segments.begin() + 1, segments.end());
    return WindowsPathKind::Absolute;
}

// Parses an absolute |path| into |out|; tells a relative one apart, leaving
// |out| alone.
WindowsPathKind ParseAbsoluteWindowsPath(const std::string& path, WindowsPath& out, std::string* error) {
    if (path.empty()) {
        SetError(error, "the path is empty");
        return WindowsPathKind::Invalid;
    }
    if (IsWindowsSeparator(path[0])) {
        return (path.size() > 1 && IsWindowsSeparator(path[1])) ? ParseUncPath(path, out, error) : ParseFullPath(path, out, error);
    }
    if (path.size() >= 2 && IsAsciiLetter(path[0]) && path[1] == ':') {
        if (path.size() > 2 && !IsWindowsSeparator(path[2])) {
            const std::string rest = path.substr(2);
            SetError(error, "'" + path + "' is relative to drive " + std::string(1, ToUpperAscii(path[0])) +
                ":'s own current directory, which Haisos does not use: write " + path.substr(0, 2) + "\\" + rest +
                " or /" + std::string(1, ToLowerAscii(path[0])) + "/" + rest);
            return WindowsPathKind::Invalid;
        }
        out.root = std::string(1, ToUpperAscii(path[0])) + ":\\";
        out.segments.clear();
        AppendWindowsSegments(path.substr(2), out.segments);
        return WindowsPathKind::Absolute;
    }
    return WindowsPathKind::Relative;
}

// A bare device name, in any case. (With an extension, NUL.txt, it is a
// device on some Windows versions and a file on others: the host says which,
// FileSystem::IsDevicePath.)
bool IsWindowsDeviceName(const std::string& name) {
    std::string stem = name;
    for (char& c : stem) {
        c = ToUpperAscii(c);
    }
    for (const char* device : {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"}) {
        if (stem == device) {
            return true;
        }
    }
    const std::string prefix = stem.substr(0, 3);
    if (prefix != "COM" && prefix != "LPT") {
        return false;
    }
    const std::string number = stem.substr(3);
    if (number.size() == 1 && number[0] >= '0' && number[0] <= '9') {
        return true;
    }
    // The superscript digits one, two and three, in UTF-8.
    return number == "\xC2\xB9" || number == "\xC2\xB2" || number == "\xC2\xB3";
}

} // namespace

bool IsDriveLetter(const std::string& segment) {
    return segment.size() == 1 && IsAsciiLetter(segment[0]);
}

bool IsFullFileSystemRoot(const std::string& physicalPath) {
#ifdef _WIN32
    if (physicalPath.empty() || !IsWindowsSeparator(physicalPath[0]) ||
        (physicalPath.size() > 1 && IsWindowsSeparator(physicalPath[1]))) {
        return false;
    }
    std::vector<std::string> segments;
    AppendWindowsSegments(physicalPath, segments);
    return segments.empty();
#else
    (void)physicalPath;
    return false;
#endif
}

std::optional<std::string> ResolveWindowsPhysicalPath(const std::string& physicalPath, const std::string& base, std::string* error) {
    WindowsPath path;
    const WindowsPathKind kind = ParseAbsoluteWindowsPath(physicalPath, path, error);
    if (kind == WindowsPathKind::Invalid) {
        return std::nullopt;
    }
    if (kind == WindowsPathKind::Relative) {
        std::string baseError;
        if (ParseAbsoluteWindowsPath(base, path, &baseError) != WindowsPathKind::Absolute) {
            SetError(error, "'" + physicalPath + "' is relative, and what it is relative to, '" + base + "', is no absolute path");
            return std::nullopt;
        }
        AppendWindowsSegments(physicalPath, path.segments);
    }
    return path.ToString();
}

std::optional<std::string> ResolvePosixPhysicalPath(const std::string& physicalPath, const std::string& base, std::string* error) {
    if (physicalPath.empty()) {
        SetError(error, "the path is empty");
        return std::nullopt;
    }
    if (physicalPath[0] == '/') {
        return physicalPath;
    }
    if (base.empty() || base[0] != '/') {
        SetError(error, "'" + physicalPath + "' is relative, and what it is relative to, '" + base + "', is no absolute path");
        return std::nullopt;
    }
    return base + (base.back() == '/' ? "" : "/") + physicalPath;
}

std::optional<std::string> ResolvePhysicalPath(const std::string& physicalPath, const std::string& base, std::string* error) {
#ifdef _WIN32
    return ResolveWindowsPhysicalPath(physicalPath, base, error);
#else
    return ResolvePosixPhysicalPath(physicalPath, base, error);
#endif
}

bool IsPlainPosixName(const std::string& name, std::string* reason) {
    if (name.find('\0') != std::string::npos) {
        SetError(reason, "it holds a NUL character");
        return false;
    }
    return true;
}

bool IsPlainWindowsName(const std::string& name, std::string* reason) {
    for (char c : name) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte < 0x20) {
            SetError(reason, byte == 0 ? "it holds a NUL character" : "it holds a control character");
            return false;
        }
        if (std::strchr("<>:\"|?*", c) != nullptr) {
            SetError(reason, std::string("it holds '") + c + "', which Windows does not take in a name");
            return false;
        }
    }
    if (!name.empty() && (name.back() == '.' || name.back() == ' ')) {
        SetError(reason, "it ends in a dot or a space, which Windows drops from a name");
        return false;
    }
    if (IsWindowsDeviceName(name)) {
        SetError(reason, "it is the name of a Windows device");
        return false;
    }
    return true;
}

bool IsPlainHostName(const std::string& name, std::string* reason) {
#ifdef _WIN32
    return IsPlainWindowsName(name, reason);
#else
    return IsPlainPosixName(name, reason);
#endif
}

} // namespace Haisos
