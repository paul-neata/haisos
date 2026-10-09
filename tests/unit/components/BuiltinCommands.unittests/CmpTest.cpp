#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"

namespace Haisos {
namespace {

// The files every cmp test compares: c1 and c2 differ at byte 6 ('e' vs 'X',
// 145 and 130 in octal), c3 is c1 cut short, e0 is empty, and p1/p2, q1/q2
// end mid-line and mid-file at other boundaries.
void MakeCmpFiles(const std::shared_ptr<IFileSystem>& fs) {
    auto write = [&](const std::string& path, const std::string& content) {
        auto file = fs->OpenFile(path, kFileOpenWriteCreateTruncate, kFileCreateMode);
        ASSERT_NE(file, nullptr) << path;
        file->Write(content.data(), content.size());
    };
    write("/c1", "abc\ndef\n");
    write("/c2", "abc\ndXf\n");
    write("/c3", "abc\n");
    write("/e0", "");
    write("/p1", "ab");
    write("/p2", "abc");
    write("/q1", "a\nb");
    write("/q2", "a\nbc");
    const std::string same(299, 'a');
    write("/b300a", same + '\0');
    write("/b300b", same + 'x');
}

} // namespace

TEST_F(BuiltinCommandsTest, CmpIdenticalAndDifferent) {
    MakeCmpFiles(root);
    const auto cmp = [&](const std::vector<std::string>& args,
                         const std::optional<std::string>& input = std::nullopt) {
        return RunCaptured("cmp", args, input, "/");
    };
    auto captured = cmp({"c1", "c1"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = cmp({"c1", "c2"});
    EXPECT_EQ(captured.out, "c1 c2 differ: char 6, line 2\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 1);

    // -b shows the differing bytes too, in octal and as diffutils writes
    // them ('e' is 145, 'X' 130).
    captured = cmp({"-b", "c1", "c2"});
    EXPECT_EQ(captured.out, "c1 c2 differ: byte 6, line 2 is 145 e 130 X\n");
    EXPECT_EQ(captured.status, 1);

    // -s says nothing about any difference.
    captured = cmp({"-s", "c1", "c2"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, CmpVerbose) {
    MakeCmpFiles(root);
    const auto cmp = [&](const std::vector<std::string>& args,
                         const std::optional<std::string>& input = std::nullopt) {
        return RunCaptured("cmp", args, input, "/");
    };
    auto captured = cmp({"-l", "c1", "c2"});
    EXPECT_EQ(captured.out, "6 145 130\n");
    EXPECT_EQ(captured.status, 1);

    // With -b, each byte is shown as diffutils writes it, the first padded to
    // four columns.
    captured = cmp({"-l", "-b", "c1", "c2"});
    EXPECT_EQ(captured.out, "6 145 e    130 X\n");
    EXPECT_EQ(captured.status, 1);

    // The byte number is as wide as the smaller file's digits: three, for two
    // 300-byte files; a NUL is 0, 'x' is 170.
    captured = cmp({"-l", "b300a", "b300b"});
    EXPECT_EQ(captured.out, "300   0 170\n");
    EXPECT_EQ(captured.status, 1);

    // Equal files compare equal in verbose mode too.
    captured = cmp({"-l", "c1", "c1"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, CmpEof) {
    MakeCmpFiles(root);
    const auto cmp = [&](const std::vector<std::string>& args,
                         const std::optional<std::string>& input = std::nullopt) {
        return RunCaptured("cmp", args, input, "/");
    };
    auto captured = cmp({"c1", "c3"});
    EXPECT_EQ(captured.err, "cmp: EOF on c3 after byte 4, line 1\n");
    EXPECT_EQ(captured.status, 1);

    captured = cmp({"e0", "c1"});
    EXPECT_EQ(captured.err, "cmp: EOF on e0 which is empty\n");
    EXPECT_EQ(captured.status, 1);

    // -l says only how far the shorter input got.
    captured = cmp({"-l", "c1", "c3"});
    EXPECT_EQ(captured.err, "cmp: EOF on c3 after byte 4\n");
    EXPECT_EQ(captured.status, 1);

    captured = cmp({"p1", "p2"});
    EXPECT_EQ(captured.err, "cmp: EOF on p1 after byte 2, in line 1\n");
    EXPECT_EQ(captured.status, 1);

    captured = cmp({"q1", "q2"});
    EXPECT_EQ(captured.err, "cmp: EOF on q1 after byte 3, in line 2\n");
    EXPECT_EQ(captured.status, 1);

    // -s prints none of the EOF messages.
    captured = cmp({"-s", "c1", "c3"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, CmpSkipAndLimit) {
    MakeCmpFiles(root);
    const auto cmp = [&](const std::vector<std::string>& args,
                         const std::optional<std::string>& input = std::nullopt) {
        return RunCaptured("cmp", args, input, "/");
    };
    // -n 5 stops before the difference at byte 6.
    auto captured = cmp({"-n", "5", "c1", "c2"});
    EXPECT_EQ(captured.status, 0);

    // -i 2 skips two bytes of both, moving the difference to byte 4.
    captured = cmp({"-i", "2", "c1", "c2"});
    EXPECT_EQ(captured.out, "c1 c2 differ: char 4, line 2\n");
    EXPECT_EQ(captured.status, 1);

    // The SKIP1/SKIP2 operands override -i; byte and line numbers count from
    // the skip, so this difference is the first byte of the compared part.
    captured = cmp({"c1", "c2", "5", "6"});
    EXPECT_EQ(captured.out, "c1 c2 differ: char 1, line 1\n");
    EXPECT_EQ(captured.status, 1);

    // A skip past both ends: nothing left to compare.
    captured = cmp({"-i", "1k", "c1", "c2"});
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, CmpStandardInput) {
    MakeCmpFiles(root);
    const auto cmp = [&](const std::vector<std::string>& args,
                         const std::optional<std::string>& input = std::nullopt) {
        return RunCaptured("cmp", args, input, "/");
    };
    // "-" is standard input, named as it was given.
    auto captured = cmp({"-", "c1"}, "abc\ndXf\n");
    EXPECT_EQ(captured.out, "- c1 differ: char 6, line 2\n");
    EXPECT_EQ(captured.status, 1);

    // With no FILE2, standard input stands in.
    captured = cmp({"c1"}, "abc\ndef\n");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, CmpErrors) {
    MakeCmpFiles(root);
    const auto cmp = [&](const std::vector<std::string>& args,
                         const std::optional<std::string>& input = std::nullopt) {
        return RunCaptured("cmp", args, input, "/");
    };
    auto captured = cmp({"c1", "missing"});
    EXPECT_EQ(captured.err, "cmp: missing: No such file or directory\n");
    EXPECT_EQ(captured.status, 2);

    // -s says nothing about trouble either, but the status stays 2.
    captured = cmp({"-s", "c1", "missing"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 2);

    captured = cmp({"c1", "/docs"});
    EXPECT_EQ(captured.err, "cmp: /docs: Is a directory\n");
    EXPECT_EQ(captured.status, 2);

    captured = cmp({"-x", "c1", "c2"});
    EXPECT_EQ(captured.err, "cmp: invalid option -- 'x'\ncmp: Try 'cmp --help' for more information.\n");
    EXPECT_EQ(captured.status, 2);

    captured = cmp({});
    EXPECT_EQ(captured.err,
        "cmp: missing operand after 'cmp'\ncmp: Try 'cmp --help' for more information.\n");
    EXPECT_EQ(captured.status, 2);

    captured = cmp({"-l", "-s", "c1", "c2"});
    EXPECT_EQ(captured.err,
        "cmp: options -l and -s are incompatible\ncmp: Try 'cmp --help' for more information.\n");
    EXPECT_EQ(captured.status, 2);

    captured = cmp({"c1", "c2", "1", "2", "3"});
    EXPECT_EQ(captured.err, "cmp: extra operand '3'\ncmp: Try 'cmp --help' for more information.\n");
    EXPECT_EQ(captured.status, 2);

    captured = cmp({"-n", "x", "c1", "c2"});
    EXPECT_EQ(captured.err, "cmp: invalid --bytes value 'x'\ncmp: Try 'cmp --help' for more information.\n");
    EXPECT_EQ(captured.status, 2);

    captured = cmp({"-i", "x", "c1", "c2"});
    EXPECT_EQ(captured.err,
        "cmp: invalid --ignore-initial value 'x'\ncmp: Try 'cmp --help' for more information.\n");
    EXPECT_EQ(captured.status, 2);

    // -v is --version.
    captured = cmp({"-v"});
    EXPECT_EQ(captured.out, "cmp (HaisosOS builtin) 1.0.0\n");
    EXPECT_EQ(captured.status, 0);
}

} // namespace Haisos