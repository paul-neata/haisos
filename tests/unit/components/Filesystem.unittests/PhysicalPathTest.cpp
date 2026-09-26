#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "PhysicalPath.h"
#include "VirtualPath.h"

using namespace Haisos;

namespace {

// ResolveWindowsPhysicalPath's result, or "<error: ...>".
std::string Windows(const std::string& path, const std::string& base = "C:\\base\\dir") {
    std::string error;
    auto resolved = ResolveWindowsPhysicalPath(path, base, &error);
    return resolved ? *resolved : "<error: " + error + ">";
}

bool WindowsRefuses(const std::string& path, const std::string& base = "C:\\base\\dir") {
    return !ResolveWindowsPhysicalPath(path, base).has_value();
}

} // namespace

// --- Windows: every way a directory may be written ---

TEST(PhysicalPathTest, AFullPathOnWindowsStartsWithItsDrive) {
    EXPECT_EQ(Windows("/c/folder1/folder2/file.txt"), "C:\\folder1\\folder2\\file.txt");
    EXPECT_EQ(Windows("\\c\\folder1\\file.txt"), "C:\\folder1\\file.txt");
    // The letter in either case, and any mix of separators.
    EXPECT_EQ(Windows("/C/folder1\\file.txt"), "C:\\folder1\\file.txt");
    EXPECT_EQ(Windows("/d"), "D:\\");
    EXPECT_EQ(Windows("/c/"), "C:\\");
    // ".." may even leave one drive for another, as in the full filesystem.
    EXPECT_EQ(Windows("/c/x/../../d/y"), "D:\\y");
}

TEST(PhysicalPathTest, ADrivePathOnWindowsTakesEitherSeparator) {
    EXPECT_EQ(Windows("c:\\folder\\file.txt"), "C:\\folder\\file.txt");
    EXPECT_EQ(Windows("c:/folder/file.txt"), "C:\\folder\\file.txt");
    EXPECT_EQ(Windows("C:\\a/b\\c"), "C:\\a\\b\\c");
    EXPECT_EQ(Windows("c:"), "C:\\");
    EXPECT_EQ(Windows("c:\\"), "C:\\");
    // "." and ".." are resolved as Windows resolves them: never above the root.
    EXPECT_EQ(Windows("C:\\a\\.\\..\\b"), "C:\\b");
    EXPECT_EQ(Windows("C:\\..\\..\\b"), "C:\\b");
}

TEST(PhysicalPathTest, AUncPathOnWindowsTakesEitherSeparator) {
    EXPECT_EQ(Windows("\\\\server\\share\\dir"), "\\\\server\\share\\dir");
    EXPECT_EQ(Windows("//server/share/dir"), "\\\\server\\share\\dir");
    EXPECT_EQ(Windows("//server/share"), "\\\\server\\share\\");
    // ".." stops at the share.
    EXPECT_EQ(Windows("\\\\server\\share\\a\\..\\..\\b"), "\\\\server\\share\\b");
}

TEST(PhysicalPathTest, ARelativePathOnWindowsIsTakenFromTheBase) {
    EXPECT_EQ(Windows("sub\\x"), "C:\\base\\dir\\sub\\x");
    EXPECT_EQ(Windows("sub/x"), "C:\\base\\dir\\sub\\x");
    EXPECT_EQ(Windows("."), "C:\\base\\dir");
    EXPECT_EQ(Windows("../sibling"), "C:\\base\\sibling");
    EXPECT_EQ(Windows("..\\..\\..\\..\\top"), "C:\\top");
    EXPECT_EQ(Windows("data", "\\\\server\\share\\project"), "\\\\server\\share\\project\\data");
    EXPECT_EQ(Windows("data", "/d/project"), "D:\\project\\data");
}

TEST(PhysicalPathTest, WhatNamesNoDirectoryOnWindowsIsRefused) {
    // The full filesystem holds nothing but drives at its top.
    EXPECT_TRUE(WindowsRefuses("/tmp/x"));
    EXPECT_NE(Windows("/tmp/x").find("no drive letter"), std::string::npos) << Windows("/tmp/x");
    EXPECT_TRUE(WindowsRefuses("/cc/x"));
    EXPECT_TRUE(WindowsRefuses("/1/x"));
    // Its root holds the drives, and is on none of them.
    EXPECT_TRUE(WindowsRefuses("/"));
    EXPECT_TRUE(WindowsRefuses("\\"));
    EXPECT_TRUE(WindowsRefuses("/c/.."));
    // Relative to drive C:'s own current directory.
    EXPECT_TRUE(WindowsRefuses("c:folder"));
    EXPECT_NE(Windows("c:folder").find("c:\\folder or /c/folder"), std::string::npos) << Windows("c:folder");
    // Device and verbatim paths, and what is no UNC path.
    EXPECT_TRUE(WindowsRefuses("\\\\.\\PhysicalDrive0"));
    EXPECT_TRUE(WindowsRefuses("\\\\?\\C:\\x"));
    EXPECT_TRUE(WindowsRefuses("//./c:/x"));
    EXPECT_TRUE(WindowsRefuses("\\\\server"));
    EXPECT_TRUE(WindowsRefuses("\\\\server\\"));
    EXPECT_TRUE(WindowsRefuses("\\\\\\server\\share"));
    EXPECT_TRUE(WindowsRefuses(""));
    // A relative path needs an absolute base.
    EXPECT_TRUE(WindowsRefuses("x", "relative\\base"));
    EXPECT_TRUE(WindowsRefuses("x", ""));
}

TEST(PhysicalPathTest, OnlyTheRootOfTheFullFileSystemIsIt) {
#ifdef _WIN32
    EXPECT_TRUE(IsFullFileSystemRoot("/"));
    EXPECT_TRUE(IsFullFileSystemRoot("\\"));
    EXPECT_TRUE(IsFullFileSystemRoot("/."));
    EXPECT_TRUE(IsFullFileSystemRoot("/c/.."));
    EXPECT_FALSE(IsFullFileSystemRoot("//server/share"));
#else
    // "/" is the disk's own root there.
    EXPECT_FALSE(IsFullFileSystemRoot("/"));
#endif
    EXPECT_FALSE(IsFullFileSystemRoot("/c"));
    EXPECT_FALSE(IsFullFileSystemRoot("."));
    EXPECT_FALSE(IsFullFileSystemRoot(""));
}

TEST(PhysicalPathTest, ADriveLetterIsOneLetter) {
    EXPECT_TRUE(IsDriveLetter("c"));
    EXPECT_TRUE(IsDriveLetter("Z"));
    EXPECT_FALSE(IsDriveLetter("cc"));
    EXPECT_FALSE(IsDriveLetter("1"));
    EXPECT_FALSE(IsDriveLetter("c:"));
    EXPECT_FALSE(IsDriveLetter(""));
}

// --- POSIX: a host path as it is ---

TEST(PhysicalPathTest, APosixPathIsJoinedToTheBaseAsItIs) {
    EXPECT_EQ(ResolvePosixPhysicalPath("/x/y", "/base").value_or("?"), "/x/y");
    EXPECT_EQ(ResolvePosixPhysicalPath("x/y", "/base").value_or("?"), "/base/x/y");
    EXPECT_EQ(ResolvePosixPhysicalPath("x", "/").value_or("?"), "/x");
    // '\' is part of a name, and ".." is left for the host, which resolves it
    // after following any link before it.
    EXPECT_EQ(ResolvePosixPhysicalPath("a\\b", "/base").value_or("?"), "/base/a\\b");
    EXPECT_EQ(ResolvePosixPhysicalPath("../x", "/base").value_or("?"), "/base/../x");
    EXPECT_FALSE(ResolvePosixPhysicalPath("", "/base").has_value());
    EXPECT_FALSE(ResolvePosixPhysicalPath("x", "relative").has_value());
}

// --- Names that reach the host as something else ---

TEST(PhysicalPathTest, WindowsNamesThatLeadElsewhereAreNotPlain) {
    for (const char* plain : {"file.txt", "caf\xC3\xA9", "a b", "console", "com10", "lpt", "nul_x", "con-1", ".hidden", "a.b.c",
                              // A device name with an extension is a file on Windows 11 and a device on
                              // Windows 10: the host decides (FileSystem::IsDevicePath), not the name.
                              "Con.txt", "nul.tar.gz", "aux.c", "COM9.log", "CONOUT$.x", "nul .txt"}) {
        EXPECT_TRUE(IsPlainWindowsName(plain)) << plain;
    }
    for (const char* elsewhere : {"c:", "file.txt:stream", "a?", "a*", "a<b", "a>b", "a|b", "a\"b", "bin.", "bin ",
                                  "con", "CON", "aux", "prn", "nul", "com1", "lpt0", "conin$", "CONOUT$", "com\xC2\xB9", "a\tb"}) {
        std::string reason;
        EXPECT_FALSE(IsPlainWindowsName(elsewhere, &reason)) << elsewhere;
        EXPECT_FALSE(reason.empty()) << elsewhere;
    }
    EXPECT_FALSE(IsPlainWindowsName(std::string("a\0b", 3)));
}

TEST(PhysicalPathTest, APosixNameIsPlainUnlessItHoldsANul) {
    for (const char* plain : {"file.txt", "a\\b", "c:", "con", "bin.", "a?"}) {
        EXPECT_TRUE(IsPlainPosixName(plain)) << plain;
    }
    EXPECT_FALSE(IsPlainPosixName(std::string("a\0b", 3)));
}

// --- Virtual paths: '/' always, and '\' too on Windows ---

TEST(VirtualPathTest, ABackslashSeparatesOnlyOnWindows) {
#ifdef _WIN32
    EXPECT_TRUE(IsVirtualPathSeparator('\\'));
    EXPECT_EQ(NormalizeVirtualPath("\\a\\b/c"), "/a/b/c");
    EXPECT_EQ(NormalizeVirtualPath("sub\\..\\..\\x", "/base"), "/x");
    EXPECT_EQ(NormalizeVirtualPath("x", "\\base\\dir"), "/base/dir/x");
#else
    EXPECT_FALSE(IsVirtualPathSeparator('\\'));
    EXPECT_EQ(NormalizeVirtualPath("/a\\b/c"), "/a\\b/c");
    EXPECT_EQ(NormalizeVirtualPath("sub\\..\\..\\x", "/base"), "/base/sub\\..\\..\\x");
#endif
    EXPECT_TRUE(IsVirtualPathSeparator('/'));
}

TEST(VirtualPathTest, SplitVirtualPathTellsWhenAPathClimbsAboveTheRoot) {
    std::vector<std::string> segments;
    EXPECT_TRUE(SplitVirtualPath("/a/./b/../c/", segments));
    EXPECT_EQ(segments, (std::vector<std::string>{"a", "c"}));
    EXPECT_TRUE(SplitVirtualPath("", segments));
    EXPECT_TRUE(segments.empty());
    EXPECT_TRUE(SplitVirtualPath("a/..", segments));
    EXPECT_TRUE(segments.empty());

    EXPECT_FALSE(SplitVirtualPath("../x", segments));
    EXPECT_FALSE(SplitVirtualPath("/a/../../x", segments));
    // NormalizeVirtualPath stays at the root instead.
    EXPECT_EQ(NormalizeVirtualPath("/a/../../x"), "/x");
}
