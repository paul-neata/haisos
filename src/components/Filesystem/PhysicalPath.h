#pragma once
#include <optional>
#include <string>

namespace Haisos {

// A physical path is how the host's disk is named to Haisos: where a physical
// filesystem is rooted (IFactory::CreatePhysicalFileSystem, a haisosfile's
// FS ... PHYSICAL), and the host files a haisosfile's COPY and OUTCOPY name.
//
// On Linux it is a host path as the host takes it: '/' separates, and '\' is
// part of a name.
//
// On Windows '\' and '/' both separate, in any mix, and a physical path is one
// of:
//   /c/x or \c\x                      a path of the full physical filesystem
//                                     (IFactory::CreateFullPhysicalFileSystem),
//                                     whose top directories are the drives:
//                                     C:\x. The letter may be in either case.
//   c:\x or c:/x, and c: alone        a path on drive C: (c: is its root)
//   \\server\share\x or //server/share/x  a UNC path, on a share of a server
//   x\y, x/y, ..\x                    relative
// Refused: a drive-relative path (c:x, which Windows takes from drive C:'s own
// current directory, a notion Haisos does not use); a \\?\ or \\.\ path; and a
// full path that names no drive (/tmp/x -- the full filesystem holds nothing
// but drives -- or / itself, which IsFullFileSystemRoot tells apart).
//
// The functions for either platform's rules are pure string handling, so both
// can be tested anywhere; ResolvePhysicalPath and IsPlainHostName follow the
// host's.

// Whether |physicalPath| names the root of the full physical filesystem: "/"
// (or "\", "/.", ...) on Windows, where that root holds the drives but is no
// directory of the host. Never on Linux, where "/" is the host's own root.
bool IsFullFileSystemRoot(const std::string& physicalPath);

// The absolute host path |physicalPath| names -- "C:\x" or "\\server\share\x"
// on Windows, "/x" on Linux -- a relative one taken from |base|, which must be
// an absolute host path (the current directory, say). Returns nullopt, and
// why in |error| if given, if it names none.
//
// On Windows "." and ".." are resolved here, lexically, as Windows itself
// resolves them, and neither climbs above a drive's root or a share; the
// result is separated by '\'. On Linux the path is only joined to |base|: the
// host resolves ".." after following a link, and so does
// PhysicalFileSystem, which canonicalizes its root.
std::optional<std::string> ResolvePhysicalPath(const std::string& physicalPath, const std::string& base, std::string* error = nullptr);
std::optional<std::string> ResolveWindowsPhysicalPath(const std::string& physicalPath, const std::string& base, std::string* error = nullptr);
std::optional<std::string> ResolvePosixPhysicalPath(const std::string& physicalPath, const std::string& base, std::string* error = nullptr);

// Whether |segment| names a drive, as the first directory of a Windows full
// path does: a single letter, in either case.
bool IsDriveLetter(const std::string& segment);

// Whether |name|, a single segment of a path, reaches the host as exactly that
// name -- and if not, why, in |reason| if given. Never one holding a NUL,
// which would end the name early. On Windows, too, never one:
//  - holding < > : " | ? * or a control character: ':' names a drive or a
//    file's alternate data stream, and ? and * are wildcards;
//  - ending in '.' or ' ', which Windows drops: "bin." would be "bin";
//  - that is a device name: CON, PRN, AUX, NUL, COM0-COM9, LPT0-LPT9 (the
//    digits 1-3 superscript, too), CONIN$ and CONOUT$. A file of that name
//    can be the device -- the console, a serial port. With an extension
//    (NUL.txt) it is one on Windows 10 and a file on Windows 11, so the host
//    decides that: see FileSystem::IsDevicePath, which PhysicalFileSystem asks
//    as well.
// Any of those would reach something other than the file the path names, past
// the mounts and builtins, which take names as they are written.
bool IsPlainHostName(const std::string& name, std::string* reason = nullptr);
bool IsPlainWindowsName(const std::string& name, std::string* reason = nullptr);
bool IsPlainPosixName(const std::string& name, std::string* reason = nullptr);

} // namespace Haisos
