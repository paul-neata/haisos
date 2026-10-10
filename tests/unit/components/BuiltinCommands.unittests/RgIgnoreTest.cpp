#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"
#include "commands/rg/RgIgnore.h"

using namespace Haisos;

namespace {

void WriteTo(const std::shared_ptr<IFileSystem>& fs, const std::string& path,
             const std::string& content) {
    auto file = fs->OpenFile(path, kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_NE(file, nullptr) << path;
    file->Write(content.data(), content.size());
}

// The plan's tree under /repo: a .git directory, a .gitignore, and the files
// it ignores or lets through. Without |withGit| there is no repository, so
// the .gitignore counts for nothing.
void MakeIgnoreTree(const std::shared_ptr<IFileSystem>& fs, bool withGit) {
    ASSERT_EQ(fs->CreateDirectory("/repo", kDirMode), 0);
    if (withGit) {
        ASSERT_EQ(fs->CreateDirectory("/repo/.git", kDirMode), 0);
    }
    WriteTo(fs, "/repo/.gitignore", "*.log\n!keep.log\nbuild/\n/top.txt\na/**/f.txt\n");
    ASSERT_EQ(fs->CreateDirectory("/repo/a", kDirMode), 0);
    ASSERT_EQ(fs->CreateDirectory("/repo/a/b", kDirMode), 0);
    ASSERT_EQ(fs->CreateDirectory("/repo/a/b/c", kDirMode), 0);
    ASSERT_EQ(fs->CreateDirectory("/repo/build", kDirMode), 0);
    ASSERT_EQ(fs->CreateDirectory("/repo/x", kDirMode), 0);
    ASSERT_EQ(fs->CreateDirectory("/repo/x/build", kDirMode), 0);
    ASSERT_EQ(fs->CreateDirectory("/repo/.hidden", kDirMode), 0);
    WriteTo(fs, "/repo/a/b/c/f.txt", "hi\n");
    WriteTo(fs, "/repo/a/keep.log", "hi\n");
    WriteTo(fs, "/repo/a/drop.log", "hi\n");
    WriteTo(fs, "/repo/a/B.TXT", "hi\n");
    WriteTo(fs, "/repo/build/o.txt", "hi\n");
    WriteTo(fs, "/repo/x/build/o.txt", "hi\n");
    WriteTo(fs, "/repo/x/build.txt", "hi\n");
    WriteTo(fs, "/repo/top.txt", "hi\n");
    WriteTo(fs, "/repo/a/b/deep.md", "hi\n");
    WriteTo(fs, "/repo/.hidden/h.txt", "hi\n");
}

// rg --files on the default tree: what survives .gitignore and the hidden
// rule.
const char* const kDefaultList =
    "a/B.TXT\na/b/deep.md\na/keep.log\nx/build.txt\n";

// The same tree with every ignore file off (-u): its nine non-hidden files.
const char* const kUnrestrictedList =
    "a/B.TXT\na/b/c/f.txt\na/b/deep.md\na/drop.log\na/keep.log\nbuild/o.txt\n"
    "top.txt\nx/build/o.txt\nx/build.txt\n";

} // namespace

// --- rg: one gitignore-syntax pattern set ---

TEST(RgGitignoreTest, Patterns) {
    {
        const RgGitignore gi = RgGitignore::Parse("*.log\n", false);
        EXPECT_EQ(gi.Match("a/drop.log", false), RgMatch::Ignore);
        EXPECT_TRUE(gi.HasWhitelist());
    }
    {
        const RgGitignore gi = RgGitignore::Parse("!keep.log\n", false);
        EXPECT_EQ(gi.Match("a/keep.log", false), RgMatch::Whitelist);
        EXPECT_FALSE(gi.HasWhitelist());
    }
    {
        // a trailing '/': directories only, at any depth
        const RgGitignore gi = RgGitignore::Parse("build/\n", false);
        EXPECT_EQ(gi.Match("build", true), RgMatch::Ignore);
        EXPECT_EQ(gi.Match("x/build", true), RgMatch::Ignore);
        EXPECT_EQ(gi.Match("x/build.txt", false), RgMatch::None);
        EXPECT_EQ(gi.Match("x/build.txt", true), RgMatch::None);
    }
    {
        // a leading '/': anchored at the file's own directory
        const RgGitignore gi = RgGitignore::Parse("/top.txt\n", false);
        EXPECT_EQ(gi.Match("top.txt", false), RgMatch::Ignore);
        EXPECT_EQ(gi.Match("a/top.txt", false), RgMatch::None);
    }
    {
        const RgGitignore gi = RgGitignore::Parse("a/**/f.txt\n", false);
        EXPECT_EQ(gi.Match("a/b/c/f.txt", false), RgMatch::Ignore);
        EXPECT_EQ(gi.Match("a/f.txt", false), RgMatch::Ignore);
    }
    {
        const RgGitignore gi = RgGitignore::Parse("**/foo\n", false);
        EXPECT_EQ(gi.Match("foo", false), RgMatch::Ignore);
        EXPECT_EQ(gi.Match("x/y/foo", false), RgMatch::Ignore);
    }
    {
        // a trailing '/**': everything below, but not the directory itself
        const RgGitignore gi = RgGitignore::Parse("abc/**\n", false);
        EXPECT_EQ(gi.Match("abc/x/y", false), RgMatch::Ignore);
        EXPECT_EQ(gi.Match("abc/x/y", true), RgMatch::Ignore);
        EXPECT_EQ(gi.Match("abc", true), RgMatch::None);
    }
    {
        const RgGitignore gi = RgGitignore::Parse("\\#x\n", false);
        EXPECT_EQ(gi.Match("#x", false), RgMatch::Ignore);
    }
    {
        const RgGitignore gi = RgGitignore::Parse("\\!x\n", false);
        EXPECT_EQ(gi.Match("!x", false), RgMatch::Ignore);
    }
    {
        // a '#' line is a comment, not a pattern
        const RgGitignore gi = RgGitignore::Parse("# c\n", false);
        EXPECT_TRUE(gi.Empty());
    }
    {
        // trailing spaces dropped
        const RgGitignore gi = RgGitignore::Parse("foo   \n", false);
        EXPECT_EQ(gi.Match("foo", false), RgMatch::Ignore);
        EXPECT_EQ(gi.Match("foo ", false), RgMatch::None);
    }
    {
        // the last matching pattern decides
        const RgGitignore gi = RgGitignore::Parse("*.log\n!keep.log\n", false);
        EXPECT_EQ(gi.Match("a/keep.log", false), RgMatch::Whitelist);
        EXPECT_EQ(gi.Match("a/drop.log", false), RgMatch::Ignore);
    }
    {
        const RgGitignore gi = RgGitignore::Parse("*.txt\n", true);
        EXPECT_EQ(gi.Match("B.TXT", false), RgMatch::Ignore);
    }
}

// --- rg: the walk's filters ---

TEST_F(BuiltinCommandsTest, RgIgnoreDefault) {
    MakeIgnoreTree(root, true);
    const Captured captured = RunCaptured("rg", {"--files"}, std::nullopt, "/repo");
    EXPECT_EQ(captured.out, kDefaultList);
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, RgIgnoreOutsideRepository) {
    // No .git: the .gitignore counts for nothing; --no-require-git reads it
    // anyway.
    MakeIgnoreTree(root, false);
    const Captured plain = RunCaptured("rg", {"--files"}, std::nullopt, "/repo");
    EXPECT_EQ(plain.out, kUnrestrictedList);
    EXPECT_EQ(plain.status, 0);
    const Captured required =
        RunCaptured("rg", {"--no-require-git", "--files"}, std::nullopt, "/repo");
    EXPECT_EQ(required.out, kDefaultList);
    EXPECT_EQ(required.status, 0);
}

TEST_F(BuiltinCommandsTest, RgIgnoreFromSubdirectory) {
    MakeIgnoreTree(root, true);
    // The parent's .gitignore applies from below ("B" sorts before "b").
    const Captured captured = RunCaptured("rg", {"--files"}, std::nullopt, "/repo/a");
    EXPECT_EQ(captured.out, "B.TXT\nb/deep.md\nkeep.log\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, RgIgnoreDotFileWins) {
    MakeIgnoreTree(root, true);
    // A .ignore whitelists where the .gitignore excludes, and a .gitignore
    // above the repository root changes nothing.
    WriteTo(root, "/repo/a/.ignore", "!drop.log\n");
    WriteTo(root, "/.gitignore", "deep.md\n");
    const Captured captured = RunCaptured("rg", {"--files"}, std::nullopt, "/repo");
    EXPECT_EQ(captured.out,
        "a/B.TXT\na/b/deep.md\na/drop.log\na/keep.log\nx/build.txt\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, RgIgnoreKinds) {
    MakeIgnoreTree(root, true);
    // .rgignore wins over .ignore, which still decides what .rgignore does
    // not; .git/info/exclude of the repository root adds its own patterns.
    WriteTo(root, "/repo/a/.ignore", "keep.log\ndeep.md\n");
    WriteTo(root, "/repo/a/.rgignore", "!keep.log\n");
    ASSERT_EQ(root->CreateDirectory("/repo/.git/info", kDirMode), 0);
    WriteTo(root, "/repo/.git/info/exclude", "B.TXT\n");
    EXPECT_EQ(RunCaptured("rg", {"--files"}, std::nullopt, "/repo").out,
        "a/keep.log\nx/build.txt\n");
    EXPECT_EQ(RunCaptured("rg", {"--no-ignore-exclude", "--files"}, std::nullopt, "/repo").out,
        "a/B.TXT\na/keep.log\nx/build.txt\n");
    EXPECT_EQ(RunCaptured("rg", {"--no-ignore-dot", "--files"}, std::nullopt, "/repo").out,
        "a/b/deep.md\na/keep.log\nx/build.txt\n");
    EXPECT_EQ(RunCaptured("rg", {"--no-ignore-vcs", "--files"}, std::nullopt, "/repo").out,
        "a/B.TXT\na/b/c/f.txt\na/drop.log\na/keep.log\nbuild/o.txt\ntop.txt\n"
        "x/build/o.txt\nx/build.txt\n");
    // The last of --no-ignore / --ignore wins.
    EXPECT_EQ(RunCaptured("rg", {"--no-ignore", "--ignore", "--files"}, std::nullopt, "/repo").out,
        "a/keep.log\nx/build.txt\n");
}

TEST_F(BuiltinCommandsTest, RgIgnoreNoParent) {
    MakeIgnoreTree(root, true);
    WriteTo(root, "/repo/.ignore", "B.TXT\n");
    // From /repo/a the parent's .ignore and .gitignore apply ...
    EXPECT_EQ(RunCaptured("rg", {"--files"}, std::nullopt, "/repo/a").out,
        "b/deep.md\nkeep.log\n");
    // ... and --no-ignore-parent respects no ignore file above the operand.
    EXPECT_EQ(RunCaptured("rg", {"--no-ignore-parent", "--files"}, std::nullopt, "/repo/a").out,
        "B.TXT\nb/c/f.txt\nb/deep.md\ndrop.log\nkeep.log\n");
}

TEST_F(BuiltinCommandsTest, RgIgnoreUnrestrictedBinary) {
    MakeIgnoreTree(root, true);
    WriteTo(root, "/repo/x/bin.dat", std::string("x\0y hi\n", 7));
    // -uuu is --no-ignore --hidden --binary: a binary file met while walking
    // is searched as an operand is, not ended silently.
    const Captured three = RunCaptured("rg", {"-uuu", "hi", "x"}, std::nullopt, "/repo");
    const Captured spelled = RunCaptured(
        "rg", {"--no-ignore", "--hidden", "--binary", "hi", "x"}, std::nullopt, "/repo");
    const Captured two = RunCaptured("rg", {"-uu", "hi", "x"}, std::nullopt, "/repo");
    EXPECT_EQ(three.out, spelled.out);
    EXPECT_NE(three.out.find("binary file matches"), std::string::npos);
    EXPECT_EQ(two.out.find("binary file matches"), std::string::npos);
}

TEST_F(BuiltinCommandsTest, RgIgnoreUnrestricted) {
    MakeIgnoreTree(root, true);
    const Captured once = RunCaptured("rg", {"-u", "--files"}, std::nullopt, "/repo");
    EXPECT_EQ(once.out, kUnrestrictedList);
    EXPECT_EQ(once.status, 0);
    // -uu searches the hidden entries too (.git holds nothing).
    const Captured twice = RunCaptured("rg", {"-uu", "--files"}, std::nullopt, "/repo");
    EXPECT_EQ(twice.out,
        ".gitignore\n.hidden/h.txt\na/B.TXT\na/b/c/f.txt\na/b/deep.md\n"
        "a/drop.log\na/keep.log\nbuild/o.txt\ntop.txt\nx/build/o.txt\nx/build.txt\n");
    EXPECT_EQ(twice.status, 0);
    const Captured hidden =
        RunCaptured("rg", {"--hidden", "--files"}, std::nullopt, "/repo");
    EXPECT_EQ(hidden.out,
        ".gitignore\n.hidden/h.txt\na/B.TXT\na/b/deep.md\na/keep.log\nx/build.txt\n");
    EXPECT_EQ(hidden.status, 0);
}

TEST_F(BuiltinCommandsTest, RgIgnoreGlobs) {
    MakeIgnoreTree(root, true);
    // The globs override .gitignore (a/b/c/f.txt and top.txt), but build/
    // stays unwalked.
    const Captured txt = RunCaptured("rg", {"-g", "*.txt", "--files"}, std::nullopt, "/repo");
    EXPECT_EQ(txt.out, "a/b/c/f.txt\ntop.txt\nx/build.txt\n");
    EXPECT_EQ(txt.status, 0);
    const Captured insensitive =
        RunCaptured("rg", {"--iglob", "*.txt", "--files"}, std::nullopt, "/repo");
    EXPECT_EQ(insensitive.out, "a/B.TXT\na/b/c/f.txt\ntop.txt\nx/build.txt\n");
    const Captured excluded =
        RunCaptured("rg", {"-g", "!*.md", "--files"}, std::nullopt, "/repo");
    EXPECT_EQ(excluded.out, "a/B.TXT\na/keep.log\nx/build.txt\n");
    // The last matching glob decides.
    const Captured last = RunCaptured(
        "rg", {"-g", "*.txt", "-g", "!top.txt", "--files"}, std::nullopt, "/repo");
    EXPECT_EQ(last.out, "a/b/c/f.txt\nx/build.txt\n");
    // --glob-case-insensitive folds the -g globs.
    const Captured folded = RunCaptured(
        "rg", {"--glob-case-insensitive", "-g", "*.txt", "--files"}, std::nullopt, "/repo");
    EXPECT_EQ(folded.out, "a/B.TXT\na/b/c/f.txt\ntop.txt\nx/build.txt\n");
}

TEST_F(BuiltinCommandsTest, RgIgnoreHiddenWhitelisted) {
    MakeIgnoreTree(root, true);
    // An '!' pattern whitelists a hidden directory past the hidden rule.
    WriteTo(root, "/repo/.ignore", "!.hidden\n");
    const Captured captured = RunCaptured("rg", {"--files"}, std::nullopt, "/repo");
    EXPECT_EQ(captured.out,
        ".hidden/h.txt\na/B.TXT\na/b/deep.md\na/keep.log\nx/build.txt\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, RgIgnoreTypes) {
    MakeIgnoreTree(root, true);
    const Captured selected = RunCaptured(
        "rg", {"-t", "txt", "-T", "md", "--files"}, std::nullopt, "/repo");
    EXPECT_EQ(selected.out, "x/build.txt\n");
    EXPECT_EQ(selected.status, 0);
    const Captured unknown = RunCaptured("rg", {"-t", "foo"}, std::nullopt, "/repo");
    EXPECT_EQ(unknown.out, "");
    EXPECT_EQ(unknown.err, "rg: unrecognized file type: foo\n");
    EXPECT_EQ(unknown.status, 2);
    // An attached -t argument, and a type aliased by another row.
    const Captured attached = RunCaptured("rg", {"-tmd", "--files"}, std::nullopt, "/repo");
    EXPECT_EQ(attached.out, "a/b/deep.md\n");
    EXPECT_EQ(attached.status, 0);
}

TEST_F(BuiltinCommandsTest, RgIgnoreMaxDepth) {
    MakeIgnoreTree(root, true);
    // The operand is depth 0, its entries 1; nothing deeper than NUM is
    // visited.
    const Captured one = RunCaptured("rg", {"-d", "1", "--files"}, std::nullopt, "/repo");
    EXPECT_EQ(one.out, "");
    EXPECT_EQ(one.status, 1);
    const Captured two = RunCaptured("rg", {"-d", "2", "--files"}, std::nullopt, "/repo");
    EXPECT_EQ(two.out, "a/B.TXT\na/keep.log\nx/build.txt\n");
    EXPECT_EQ(two.status, 0);
}

TEST_F(BuiltinCommandsTest, RgIgnoreOperandsNotFiltered) {
    MakeIgnoreTree(root, true);
    // top.txt is gitignored and build/ unwalked, but operands are never
    // filtered.
    const Captured captured = RunCaptured(
        "rg", {"--files", "top.txt", "build"}, std::nullopt, "/repo");
    EXPECT_EQ(captured.out, "top.txt\nbuild/o.txt\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, RgIgnoreNothingSearched) {
    MakeIgnoreTree(root, true);
    const Captured captured =
        RunCaptured("rg", {"-g", "*.zzz", "x"}, std::nullopt, "/repo");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err,
        "rg: No files were searched, which means ripgrep probably applied a filter "
        "you didn't expect.\nRunning with --debug will show why files are being "
        "skipped.\n");
    EXPECT_EQ(captured.status, 2);
}

TEST_F(BuiltinCommandsTest, RgIgnoreFollowAccepted) {
    MakeIgnoreTree(root, true);
    const Captured captured = RunCaptured("rg", {"-L", "--files"}, std::nullopt, "/repo");
    EXPECT_EQ(captured.out, kDefaultList);
    EXPECT_EQ(captured.err, "");  // no "not treated" report
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, RgTypeList) {
    const Captured captured = RunCaptured("rg", {"--type-list"}, std::nullopt, "/repo");
    EXPECT_EQ(captured.out,
        "c: *.[chH], *.[chH].in, *.cats\n"
        "cmake: *.cmake, CMakeLists.txt\n"
        "config: *.cfg, *.conf, *.config, *.ini\n"
        "cpp: *.[ChH], *.[ChH].in, *.[ch]pp, *.[ch]pp.in, *.[ch]xx, *.[ch]xx.in, "
        "*.cc, *.cc.in, *.hh, *.hh.in, *.inl\n"
        "cs: *.cs\n"
        "css: *.css, *.scss\n"
        "csv: *.csv\n"
        "docker: *Dockerfile*\n"
        "go: *.go\n"
        "h: *.h, *.hh, *.hpp\n"
        "html: *.ejs, *.htm, *.html\n"
        "java: *.java, *.jsp, *.jspx, *.properties\n"
        "js: *.cjs, *.js, *.jsx, *.mjs, *.vue\n"
        "json: *.json, *.sarif, composer.lock\n"
        "jsonl: *.jsonl\n"
        "kotlin: *.kt, *.kts\n"
        "lua: *.lua\n"
        "make: *.mak, *.mk, [Gg][Nn][Uu]makefile, [Gg][Nn][Uu]makefile.am, "
        "[Gg][Nn][Uu]makefile.in, [Mm]akefile, [Mm]akefile.am, [Mm]akefile.in\n"
        "markdown: *.markdown, *.md, *.mdown, *.mdwn, *.mdx, *.mkd, *.mkdn\n"
        "md: *.markdown, *.md, *.mdown, *.mdwn, *.mdx, *.mkd, *.mkdn\n"
        "php: *.php, *.php3, *.php4, *.php5, *.php7, *.php8, *.pht, *.phtml\n"
        "py: *.py, *.pyi\n"
        "python: *.py, *.pyi\n"
        "readme: *README, README*\n"
        "ruby: *.gemspec, *.rb, *.rbw, .irbrc, Gemfile, Rakefile, config.ru\n"
        "rust: *.rs\n"
        "sh: *.bash, *.bashrc, *.csh, *.cshrc, *.ksh, *.kshrc, *.sh, *.tcsh, "
        "*.zsh, .bash_login, .bash_logout, .bash_profile, .bashrc, .cshrc, "
        ".kshrc, .login, .logout, .profile, .tcshrc, .zlogin, .zlogout, "
        ".zprofile, .zshenv, .zshrc, bash_login, bash_logout, bash_profile, "
        "bashrc, profile, zlogin, zlogout, zprofile, zshenv, zshrc\n"
        "sql: *.psql, *.sql\n"
        "swift: *.swift\n"
        "toml: *.toml, Cargo.lock\n"
        "ts: *.cts, *.mts, *.ts, *.tsx\n"
        "txt: *.txt\n"
        "typescript: *.cts, *.mts, *.ts, *.tsx\n"
        "xml: *.dtd, *.rng, *.sch, *.xhtml, *.xjb, *.xml, *.xml.dist, *.xsd, "
        "*.xsl, *.xslt\n"
        "yaml: *.yaml, *.yml\n"
        "zig: *.zig\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}