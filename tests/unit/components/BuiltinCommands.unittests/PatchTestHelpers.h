#pragma once
#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"

namespace Haisos {

// The patch every patch test's expectations were observed from GNU patch
// 2.7.6 against (LC_ALL=C, no terminal); P1 is the plan's recurring example.
inline constexpr char kP1[] =
    "--- f\n"
    "+++ g\n"
    "@@ -2,7 +2,7 @@\n"
    " 2\n"
    " 3\n"
    " 4\n"
    "-5\n"
    "+five\n"
    " 6\n"
    " 7\n"
    " 8\n";

// The lines `1\n` .. `10\n`.
inline std::string Seq(size_t count) {
    std::string text;
    for (size_t i = 1; i <= count; ++i) {
        text += std::to_string(i) + "\n";
    }
    return text;
}

// The directory the patch tests work in, with a file written into it.
inline void MakePatchDir(const std::shared_ptr<IFileSystem>& fs) {
    ASSERT_EQ(fs->CreateDirectory("/p", kDirMode), 0);
}

inline void WritePatchFile(const std::shared_ptr<IFileSystem>& fs, const std::string& name,
                           const std::string& content) {
    auto file = fs->OpenFile("/p/" + name, kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_NE(file, nullptr) << name;
    file->Write(content.data(), content.size());
}

inline std::string ReadPatchFile(const std::shared_ptr<IFileSystem>& fs,
                                 const std::string& name) {
    std::string content;
    ReadWholeFile(*fs, "/p/" + name, content);
    return content;
}

inline bool PatchFileExists(const std::shared_ptr<IFileSystem>& fs, const std::string& name) {
    FileStatus status;
    return fs->Stat("/p/" + name, status) == 0;
}

} // namespace Haisos