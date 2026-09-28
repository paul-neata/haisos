#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "PhysicalFileSystem.h"

namespace Haisos {

// The whole of a Windows machine's disks as one filesystem, the way Cygwin and
// MSYS2 show them: its root holds a directory for each drive, named by the
// drive's letter in lowercase, and /c/Users/x.txt is C:\Users\x.txt. It is
// what IFactory::CreateFullPhysicalFileSystem() returns on Windows, and what
// PhysicalFileSystem::Create("/") returns there; on Linux, whose disk has one
// root already, both are a PhysicalFileSystem at "/". Windows only.
//
//  - Listing / gives every drive there is (GetLogicalDriveStringsW), mapped
//    network drives and SUBST ones included. A UNC path, on no drive, is not
//    in it.
//  - Listing a drive that takes removable media -- a removable or an optical
//    one, as GetDriveTypeW says -- with no medium in gives no files, as does
//    Stat say it is an empty directory; nothing waits on a "no disk" dialog
//    (NoCriticalErrorDialogs).
//  - Below a drive every path is PhysicalFileSystem's, taken segment by
//    segment, each a plain name. A link is followed wherever it leads, so
//    long as that is on a drive -- from one drive to another, too -- and
//    never ends a path dangling.
//  - Nothing is created or removed at the top: no drive can be made or
//    removed there, nor can a drive's root.
//  - A drive letter may be written in either case: /C/x is /c/x, as Windows
//    takes C:\x and c:\x alike. (So is the rest of a path, on a disk that
//    ignores case -- while mounts and builtins over this filesystem take
//    names as written, like on every other.)
class WindowsFullPhysicalFileSystem : public PhysicalFileSystem {
public:
    static std::shared_ptr<WindowsFullPhysicalFileSystem> Create();
    ~WindowsFullPhysicalFileSystem() override = default;

    std::vector<DirectoryEntry> LocalReadDirectory(const std::string& path) override;
    int LocalStat(const std::string& path, FileStatus& out) override;

protected:
    // /c/x/y is C:\x\y; the top itself, and a drive there is not, lie
    // nowhere on the host.
    bool HostPathOf(const std::vector<std::string>& segments, std::filesystem::path& hostPath) const override;
    // Anywhere on a drive.
    bool IsWithin(const std::string& canonical) const override;

private:
    WindowsFullPhysicalFileSystem();

    // The letters of the drives there are, lowercase, in order.
    static std::vector<char> DriveLetters();
    // Whether |letter| names a drive there is, in either case.
    static bool DriveExists(char letter);
    // Whether the drive |letter| names takes removable media and has none in.
    static bool HasNoMedium(char letter);

    // Where the answer for a path comes from.
    enum class Place {
        Top,        // "/": this filesystem, which holds the drives
        EmptyDrive, // a drive with no medium in: an empty directory
        Host,       // anything else: the host, through PhysicalFileSystem
    };
    static Place PlaceOf(const std::string& path);

    // When this filesystem was made: the times of the top, and of a drive
    // with no medium in, which have none of their own.
    const FileDateTime m_createdTime;
};

} // namespace Haisos
