#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "commands/hsh/HshGlob.h"
#include "commands/hsh/HshPattern.h"

using namespace Haisos::Hsh;
using Haisos::DirectoryEntry;
using Haisos::DirectoryEntryType;

namespace {

DirectoryEntry File_(std::string name) { return DirectoryEntry{std::move(name), DirectoryEntryType::File}; }
DirectoryEntry Dir_(std::string name) { return DirectoryEntry{std::move(name), DirectoryEntryType::Dir}; }

// One directory per map key, each listing '.' and '..' first, as
// IFileIO::ReadDirectory does. Every ReadDirectory path is recorded.
class FakePathnameSource : public IPathnameSource {
public:
    std::map<std::string, std::vector<DirectoryEntry>> dirs;
    std::vector<std::string> reads;

    std::vector<DirectoryEntry> ReadDirectory(const std::string& path) override {
        reads.push_back(path);
        auto it = dirs.find(path);
        return it == dirs.end() ? std::vector<DirectoryEntry>{} : it->second;
    }

    bool Exists(const std::string& path) override {
        std::string p = path;
        while (p.size() > 1 && p.back() == '/') p.pop_back();
        size_t slash = p.find_last_of('/');
        std::string parent, name;
        if (slash == std::string::npos) { parent = "."; name = p; }
        else if (slash == 0) { parent = "/"; name = p.substr(1); }
        else { parent = p.substr(0, slash); name = p.substr(slash + 1); }
        auto it = dirs.find(parent);
        if (it == dirs.end()) return false;
        for (const DirectoryEntry& entry : it->second)
            if (entry.name == name) return true;
        return false;
    }
};

FakePathnameSource WorkingDirSource() {
    FakePathnameSource source;
    source.dirs["."] = {
        Dir_("."), Dir_(".."),
        File_("B"), File_("C"), File_("a"), File_("ab"), File_("a b"),
        File_(".hid"), Dir_("d1"), Dir_("d2"),
    };
    return source;
}

FakePathnameSource TreeSource() {
    FakePathnameSource source;
    source.dirs["."] = {Dir_("."), Dir_(".."), File_("B"), File_("b"), Dir_("d1"), Dir_("d2")};
    source.dirs["d1"] = {Dir_("."), Dir_(".."), Dir_("e"), File_("x")};
    source.dirs["d1/e"] = {Dir_("."), Dir_(".."), File_("f")};
    source.dirs["d2"] = {Dir_("."), Dir_(".."), File_("f"), Dir_("e")};
    source.dirs["d2/e"] = {Dir_("."), Dir_(".."), File_("g")};
    source.dirs["d1/.."] = source.dirs["."];
    source.dirs["/"] = {Dir_("."), Dir_(".."), File_("a"), File_("b1"), File_("b2")};
    return source;
}

TEST(HshGlobTest, MatchesInTheWorkingDirectory) {
    FakePathnameSource source = WorkingDirSource();
    EXPECT_EQ(ExpandPathname("*", source),
              (std::vector<std::string>{"B", "C", "a", "a b", "ab", "d1", "d2"}));
    EXPECT_EQ(ExpandPathname("a?", source), (std::vector<std::string>{"ab"}));
    EXPECT_EQ(ExpandPathname("[!a]*", source), (std::vector<std::string>{"B", "C", "d1", "d2"}));
    EXPECT_EQ(ExpandPathname(".*", source), (std::vector<std::string>{".", "..", ".hid"}));
    EXPECT_TRUE(ExpandPathname("nomatch*", source).empty());
}

TEST(HshGlobTest, BracketNeverSpansASlash) {
    FakePathnameSource source = WorkingDirSource();
    // "[a/b]" is not one bracket expression matching "a": the '/' splits the
    // pattern, so the components "[a" and "b]" match nothing.
    EXPECT_TRUE(ExpandPathname("[a/b]", source).empty());
    // An ordinary bracket without a '/' is unaffected.
    EXPECT_EQ(ExpandPathname("[ab]", source), (std::vector<std::string>{"a"}));
}

TEST(HshGlobTest, Components) {
    FakePathnameSource source = TreeSource();

    EXPECT_EQ(ExpandPathname("*/f", source), (std::vector<std::string>{"d2/f"}));
    EXPECT_EQ(source.reads, (std::vector<std::string>{"."}));

    source.reads.clear();
    EXPECT_EQ(ExpandPathname("*/*/f", source), (std::vector<std::string>{"d1/e/f"}));
    EXPECT_EQ(source.reads, (std::vector<std::string>{".", "d1", "d2"}));

    source.reads.clear();
    EXPECT_EQ(ExpandPathname("d*/", source), (std::vector<std::string>{"d1/", "d2/"}));
    EXPECT_EQ(source.reads, (std::vector<std::string>{"."}));

    source.reads.clear();
    EXPECT_EQ(ExpandPathname("d1/*/", source), (std::vector<std::string>{"d1/e/"}));
    EXPECT_EQ(source.reads, (std::vector<std::string>{"d1"}));

    source.reads.clear();
    EXPECT_EQ(ExpandPathname("./*", source),
              (std::vector<std::string>{"./B", "./b", "./d1", "./d2"}));
    EXPECT_EQ(source.reads, (std::vector<std::string>{"."}));

    // Literal components are not read: only "*" reads, as "d1/..".
    source.reads.clear();
    EXPECT_EQ(ExpandPathname("d1/../*", source),
              (std::vector<std::string>{"d1/../B", "d1/../b", "d1/../d1", "d1/../d2"}));
    EXPECT_EQ(source.reads, (std::vector<std::string>{"d1/.."}));

    source.reads.clear();
    EXPECT_EQ(ExpandPathname("/b*", source), (std::vector<std::string>{"/b1", "/b2"}));
    EXPECT_EQ(source.reads, (std::vector<std::string>{"/"}));
}

TEST(HshGlobTest, EscapedCharactersAreLiteral) {
    FakePathnameSource source;
    source.dirs["."] = {Dir_("."), Dir_(".."), File_("a*"), File_("[a]")};
    EXPECT_FALSE(HasPatternCharacters("a\\*"));
    EXPECT_EQ(ExpandPathname("a\\*", source), (std::vector<std::string>{"a*"}));
    EXPECT_TRUE(source.reads.empty());
    EXPECT_FALSE(HasPatternCharacters("\\[a]"));
    EXPECT_EQ(ExpandPathname("\\[a]", source), (std::vector<std::string>{"[a]"}));
    EXPECT_TRUE(source.reads.empty());
}

} // namespace
