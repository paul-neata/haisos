#pragma once
// The tree and the helpers every find test walks (FindTest.cpp and
// FindActionsTest.cpp), taken out of FindTest.cpp when the actions needed
// them too.
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {

// The tree every find test walks: /proj/a holding b/ (which holds empty)
// and the files x.txt and y.md, and /proj/z.h. Runs work in /proj, so that
// is where the relative starting points and the default "." are.
inline void MakeProj(const std::shared_ptr<IFileSystem>& fs) {
    ASSERT_EQ(fs->CreateDirectory("/proj", kDirMode), 0);
    ASSERT_EQ(fs->CreateDirectory("/proj/a", kDirMode), 0);
    ASSERT_EQ(fs->CreateDirectory("/proj/a/b", kDirMode), 0);
    auto write = [&](const std::string& path, const std::string& content) {
        auto file = fs->OpenFile(path, kFileOpenWriteCreateTruncate, kFileCreateMode);
        ASSERT_NE(file, nullptr) << path;
        file->Write(content.data(), content.size());
    };
    write("/proj/a/x.txt", "hi\n");
    write("/proj/a/b/empty", "");
    write("/proj/a/y.md", std::string(1100, 'y'));
    write("/proj/z.h", "h\n");
}

// The default listing of /proj: every file once, "." first, a parent before
// its children. The order among the entries of one directory is the
// filesystem's own (GNU's is readdir's), so tests compare that as a set.
inline const char* kProjTree =
    ".\n./a\n./a/b\n./a/b/empty\n./a/x.txt\n./a/y.md\n./z.h\n";

// Two outputs with the same lines, whatever order the filesystem listed
// each directory's entries in. The order find itself is responsible for (a
// parent before its children, -depth's, the starting points' own order) is
// asserted on its own.
inline void ExpectSameLines(const std::string& actual, const std::string& expected) {
    Lines left = SplitLines(actual);
    Lines right = SplitLines(expected);
    std::sort(left.begin(), left.end());
    std::sort(right.begin(), right.end());
    EXPECT_EQ(left, right);
}

// Sets a path's access and modification times now plus |offsetSeconds|
// (negative: that long ago). The change time becomes now, as utimensat's.
inline void SetFileTimes(const std::shared_ptr<IFileSystem>& fs, const std::string& path,
                         int64_t offsetSeconds) {
    FileDateTime when = CurrentFileDateTime();
    when.seconds += offsetSeconds;
    ASSERT_EQ(fs->SetTimes(path, when, when), 0) << path;
}

} // namespace Haisos