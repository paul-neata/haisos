#include "WindowsFullPhysicalFileSystem.h"
#include <cwchar>
#include <iterator>
#include "FilesystemUtils.h"
#include "NoCriticalErrorDialogs.h"
#include "PhysicalPath.h"
#include "VirtualPath.h"
#include <windows.h>
#undef CreateDirectory
#undef RemoveDirectory
#undef GetCurrentDirectory

namespace Haisos {

namespace {

char ToLowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

char ToUpperAscii(char c) {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

// The root directory of the drive |letter| names, as Windows writes it: "C:\".
std::wstring DriveRoot(char letter) {
    return std::wstring{static_cast<wchar_t>(ToUpperAscii(letter)), L':', L'\\'};
}

} // namespace

std::shared_ptr<WindowsFullPhysicalFileSystem> WindowsFullPhysicalFileSystem::Create() {
    return std::shared_ptr<WindowsFullPhysicalFileSystem>(new WindowsFullPhysicalFileSystem());
}

WindowsFullPhysicalFileSystem::WindowsFullPhysicalFileSystem()
    : m_createdTime(CurrentFileDateTime())
{
}

std::vector<char> WindowsFullPhysicalFileSystem::DriveLetters() {
    // "C:\" and a NUL for each of 26 drives at most, then the closing NUL.
    wchar_t buffer[26 * 4 + 1];
    const DWORD length = ::GetLogicalDriveStringsW(static_cast<DWORD>(std::size(buffer)), buffer);
    std::vector<char> letters;
    if (length == 0 || length >= std::size(buffer)) {
        return letters;
    }
    for (const wchar_t* drive = buffer; *drive != L'\0'; drive += std::wcslen(drive) + 1) {
        const wchar_t letter = drive[0];
        if ((letter >= L'A' && letter <= L'Z') || (letter >= L'a' && letter <= L'z')) {
            letters.push_back(ToLowerAscii(static_cast<char>(letter)));
        }
    }
    return letters;
}

bool WindowsFullPhysicalFileSystem::DriveExists(char letter) {
    const char upper = ToUpperAscii(letter);
    if (upper < 'A' || upper > 'Z') {
        return false;
    }
    return ((::GetLogicalDrives() >> (upper - 'A')) & 1u) != 0;
}

bool WindowsFullPhysicalFileSystem::HasNoMedium(char letter) {
    NoCriticalErrorDialogs noDialogs;
    const std::wstring root = DriveRoot(letter);
    const UINT type = ::GetDriveTypeW(root.c_str());
    if (type != DRIVE_REMOVABLE && type != DRIVE_CDROM) {
        return false;
    }
    // A drive with no medium in has no volume to describe.
    return !::GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, nullptr, nullptr, nullptr, 0);
}

WindowsFullPhysicalFileSystem::Place WindowsFullPhysicalFileSystem::PlaceOf(const std::string& path) {
    std::vector<std::string> segments;
    if (!SplitVirtualPath(path, segments)) {
        // Climbing above the top: PhysicalFileSystem refuses it.
        return Place::Host;
    }
    if (segments.empty()) {
        return Place::Top;
    }
    if (segments.size() == 1 && IsDriveLetter(segments[0]) && DriveExists(segments[0][0]) && HasNoMedium(segments[0][0])) {
        return Place::EmptyDrive;
    }
    return Place::Host;
}

bool WindowsFullPhysicalFileSystem::HostPathOf(const std::vector<std::string>& segments, std::filesystem::path& hostPath) const {
    if (segments.empty() || !IsDriveLetter(segments[0]) || !DriveExists(segments[0][0])) {
        return false;
    }
    std::filesystem::path joined(DriveRoot(segments[0][0]));
    for (size_t i = 1; i < segments.size(); ++i) {
        joined /= std::filesystem::u8path(segments[i]);
    }
    hostPath = joined;
    return true;
}

bool WindowsFullPhysicalFileSystem::IsWithin(const std::string& canonical) const {
    // "X:\...": on a drive. A link may lead from one to another, but not to a
    // share (\\server\share), which no drive holds.
    return canonical.size() >= 3 &&
        ((canonical[0] >= 'A' && canonical[0] <= 'Z') || (canonical[0] >= 'a' && canonical[0] <= 'z')) &&
        canonical[1] == ':' && canonical[2] == '\\';
}

std::vector<DirectoryEntry> WindowsFullPhysicalFileSystem::LocalReadDirectory(const std::string& path) {
    switch (PlaceOf(path)) {
        case Place::Top: {
            std::vector<DirectoryEntry> drives;
            for (char letter : DriveLetters()) {
                drives.push_back(DirectoryEntry{std::string(1, letter), DirectoryEntryType::Dir});
            }
            return drives;
        }
        case Place::EmptyDrive:
            return {};
        case Place::Host:
            break;
    }
    return PhysicalFileSystem::LocalReadDirectory(path);
}

int WindowsFullPhysicalFileSystem::LocalStat(const std::string& path, FileStatus& out) {
    const Place place = PlaceOf(path);
    if (place == Place::Host) {
        return PhysicalFileSystem::LocalStat(path, out);
    }
    FileStatus status;
    status.type = DirectoryEntryType::Dir;
    // A directory's links: its own entry, its "." and one ".." per
    // directory in it.
    status.linkCount = 2 + (place == Place::Top ? DriveLetters().size() : 0);
    status.accessTime = status.modificationTime = status.changeTime = m_createdTime;
    out = status;
    return 0;
}

} // namespace Haisos
