#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinText.h"
#include "commands/grep/GrepMatcher.h"
#include "commands/rg/RgRegex.h"
#include "commands/rg/RgSearch.h"
#include "commands/rg/RgTypes.h"
#include "interfaces/IFileDescriptor.h"
#include "interfaces/IFileIO.h"

namespace Haisos {
namespace {

enum RgOptionId {
    kPatterns = 1,       // -e --regexp
    kPatternFile,        // -f --file
    kCaseSensitive,      // -s
    kIgnoreCase,         // -i
    kSmartCase,          // -S
    kFixedStrings,       // -F
    kNoFixedStrings,     // --no-fixed-strings
    kInvert,             // -v
    kNoInvert,           // --no-invert-match
    kLineRegexp,         // -x
    kWordRegexp,         // -w
    kMaxCount,           // -m --max-count
    kText,               // -a
    kNoText,             // --no-text
    kBinary,             // --binary
    kNoBinary,           // --no-binary
    kAfterContext,       // -A
    kBeforeContext,      // -B
    kContext,            // -C
    kByteOffset,         // -b
    kNoByteOffset,       // --no-byte-offset
    kColumn,             // --column
    kNoColumn,           // --no-column
    kColor,              // --color
    kContextSeparator,   // --context-separator
    kNoContextSeparator, // --no-context-separator
    kHeading,            // --heading
    kNoHeading,          // --no-heading
    kLineNumber,         // -n
    kNoLineNumber,       // -N
    kMaxColumns,         // -M
    kNull,               // -0
    kOnlyMatching,       // -o
    kPretty,             // -p
    kQuiet,              // -q
    kWithFilename,       // -H
    kNoFilename,         // -I
    kCount,              // -c
    kCountMatches,       // --count-matches
    kFilesWithMatches,   // -l
    kFilesWithoutMatch,  // --files-without-match
    kIncludeZero,        // --include-zero
    kNoIncludeZero,      // --no-include-zero
    kLineBuffered,       // --line-buffered
    kNoLineBuffered,     // --no-line-buffered
    kBlockBuffered,      // --block-buffered
    kNoBlockBuffered,    // --no-block-buffered
    kNoMessages,         // --no-messages
    kMessages,           // --messages
    kFiles,              // --files
    kSort,               // --sort
    kSortFiles,          // --sort-files
    kNoSortFiles,        // --no-sort-files
    kFollow,             // -L --follow (accepted: no effect)
    kNoFollow,           // --no-follow
    kGlob,               // -g --glob
    kGlobCaseInsensitive,  // --glob-case-insensitive
    kNoGlobCaseInsensitive,  // --no-glob-case-insensitive
    kIglob,              // --iglob
    kHidden,             // -. --hidden
    kNoHidden,           // --no-hidden
    kMaxDepth,           // -d --max-depth (maxdepth its hidden alias)
    kNoIgnore,           // --no-ignore
    kIgnore,             // --ignore
    kNoIgnoreDot,        // --no-ignore-dot
    kIgnoreDot,          // --ignore-dot
    kNoIgnoreExclude,    // --no-ignore-exclude
    kIgnoreExclude,      // --ignore-exclude
    kNoIgnoreParent,     // --no-ignore-parent
    kIgnoreParent,       // --ignore-parent
    kNoIgnoreVcs,        // --no-ignore-vcs
    kIgnoreVcs,          // --ignore-vcs
    kNoRequireGit,       // --no-require-git
    kRequireGit,         // --require-git
    kType,               // -t --type
    kTypeNot,            // -T --type-not
    kTypeList,           // --type-list
    kUnrestricted,       // -u
};

// rg's message for one pattern that cannot be a regex: its four lines
// (a blank one among them), on stderr, with no Try line after.
void MultilinePatternError(BuiltinContext& context) {
    context.ErrorText("rg: the literal \"\\n\" is not allowed in a regex\n"
        "\n"
        "Consider enabling multiline mode with the --multiline flag (or -U for short).\n"
        "When multiline mode is enabled, new line characters can be matched.\n");
}

// A literal for -F, as one Rust-syntax regex: every ASCII byte that is not
// [A-Za-z0-9_] written \xhh, so no metacharacter of the pattern is read. A
// byte above 0x7f stays as it is: \xhh there would be a code point, its
// UTF-8 bytes, to TranslateRgPattern -- not the byte itself.
std::string EscapeLiteral(const std::string& pattern) {
    std::string out;
    for (const char c : pattern) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if ((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z')
            || (byte >= '0' && byte <= '9') || byte == '_' || byte >= 0x80) {
            out += c;
        } else {
            char hex[8];
            std::snprintf(hex, sizeof(hex), "\\x%02x", byte);
            out += hex;
        }
    }
    return out;
}

// The value a "--name=value" argument gave |longName| (which the parser
// reports without it): the first such argument naming it by prefix, so the
// "unexpected argument" message can quote it.
std::string AttachedValue(const std::vector<std::string>& args, const std::string& longName) {
    for (const auto& arg : args) {
        if (arg == "--") {
            break;
        }
        if (arg.compare(0, 2, "--") != 0) {
            continue;
        }
        const size_t eq = arg.find('=');
        if (eq == std::string::npos || eq <= 2) {
            continue;
        }
        const std::string given = arg.substr(2, eq - 2);
        if (longName.compare(0, given.size(), given) == 0) {
            return arg.substr(eq + 1);
        }
    }
    return "";
}

// rg, on a usage error: one "rg: ..." line (or the CLI-arguments shape), no
// Try line, exit 2.
void TranslateParseError(BuiltinContext& context, const std::vector<std::string>& args,
                         const std::string& error) {
    const auto startsWith = [&](const char* prefix) {
        return error.compare(0, std::string(prefix).size(), prefix) == 0;
    };
    if (startsWith("unrecognized option '")) {
        const std::string flag = error.substr(21, error.size() - 22);
        context.ErrorText("rg: unrecognized flag " + flag + "\n");
        return;
    }
    if (startsWith("option '") && error.find("' is ambiguous") != std::string::npos) {
        const size_t end = error.find('\'', 8);
        context.ErrorText("rg: unrecognized flag " + error.substr(8, end - 8) + "\n");
        return;
    }
    if (startsWith("invalid option -- '")) {
        context.ErrorText("rg: unrecognized flag -" + error.substr(error.size() - 2, 1) + "\n");
        return;
    }
    if (startsWith("option requires an argument -- '")) {
        const std::string flag = "-" + error.substr(error.size() - 2, 1);
        context.ErrorText("rg: missing value for flag " + flag
            + ": missing argument for option '" + flag + "'\n");
        return;
    }
    if (startsWith("option '--") && error.find("' requires an argument") != std::string::npos) {
        const size_t end = error.find('\'', 9);
        const std::string flag = error.substr(8, end - 8);
        context.ErrorText("rg: missing value for flag " + flag
            + ": missing argument for option '" + flag + "'\n");
        return;
    }
    if (startsWith("option '--") && error.find("' doesn't allow an argument") != std::string::npos) {
        const size_t end = error.find('\'', 9);
        const std::string flag = error.substr(8, end - 8);
        const std::string value = AttachedValue(args, flag.substr(2));
        context.ErrorText("rg: invalid CLI arguments: unexpected argument for option '"
            + flag + "': \"" + value + "\"\n");
        return;
    }
    context.ErrorText("rg: " + error + "\n");
}

// rg's message for a number a flag cannot take, exit 2.
bool NumberError(BuiltinContext& context, const std::string& spelling, const char* detail) {
    context.ErrorText("rg: error parsing flag " + spelling + ": value is not a valid number: "
        + detail + "\n");
    return false;
}

// A decimal number of a flag (-A -B -C -m -M): digits only, saturating; the
// message rg prints (through NumberError) says which.
bool ParseNumberFlag(BuiltinContext& context, const std::string& text, const std::string& spelling,
                     uint64_t& out) {
    if (text.empty()) {
        return NumberError(context, spelling, "cannot parse integer from empty string");
    }
    uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return NumberError(context, spelling, "invalid digit found in string");
        }
        const uint64_t digit = static_cast<uint64_t>(c - '0');
        if (value > (UINT64_MAX - digit) / 10) {
            return NumberError(context, spelling, "number too large to fit in target type");
        }
        value = value * 10 + digit;
    }
    out = value;
    return true;
}

class RgCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "rg"; }
    std::string Version() const override { return "1.2.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            // Input options.
            {'e', "regexp", kPatterns, BuiltinArgument::Required, "PATTERN",
                "a pattern to search for (repeatable)"},
            {'f', "file", kPatternFile, BuiltinArgument::Required, "PATTERNFILE",
                "read patterns from a file, one per line (-: standard input)"},
            {0, "pre", kBuiltinNotTreated, BuiltinArgument::Required, "COMMAND", ""},
            {0, "no-pre", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {0, "pre-glob", kBuiltinNotTreated, BuiltinArgument::Required, "GLOB", ""},
            {'z', "search-zip", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "no-search-zip", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            // Search options.
            {'s', "case-sensitive", kCaseSensitive, BuiltinArgument::None, "",
                "search case sensitively (the default)"},
            {0, "crlf", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "no-crlf", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {0, "dfa-size-limit", kBuiltinNotTreated, BuiltinArgument::Required, "NUM", ""},
            {'E', "encoding", kBuiltinNotTreated, BuiltinArgument::Required, "ENCODING", ""},
            {0, "no-encoding", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {0, "engine", kBuiltinNotTreated, BuiltinArgument::Required, "ENGINE", ""},
            {'F', "fixed-strings", kFixedStrings, BuiltinArgument::None, "",
                "treat the patterns as literals"},
            {0, "no-fixed-strings", kNoFixedStrings, BuiltinArgument::None, "", "", true},
            {'i', "ignore-case", kIgnoreCase, BuiltinArgument::None, "",
                "search case insensitively"},
            {'v', "invert-match", kInvert, BuiltinArgument::None, "",
                "print lines that do not match"},
            {0, "no-invert-match", kNoInvert, BuiltinArgument::None, "", "", true},
            {'x', "line-regexp", kLineRegexp, BuiltinArgument::None, "",
                "only lines the patterns match wholly match"},
            {'m', "max-count", kMaxCount, BuiltinArgument::Required, "NUM",
                "stop after NUM matching lines per file"},
            {0, "mmap", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "no-mmap", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {'U', "multiline", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "no-multiline", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {0, "multiline-dotall", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "no-multiline-dotall", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {0, "no-unicode", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "unicode", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {0, "null-data", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {'P', "pcre2", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "no-pcre2", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {0, "regex-size-limit", kBuiltinNotTreated, BuiltinArgument::Required, "NUM", ""},
            {'S', "smart-case", kSmartCase, BuiltinArgument::None, "",
                "search insensitively when no pattern holds an uppercase letter"},
            {0, "stop-on-nonmatch", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {'a', "text", kText, BuiltinArgument::None, "",
                "search binary files as if they were text"},
            {0, "no-text", kNoText, BuiltinArgument::None, "", "", true},
            {'j', "threads", kBuiltinNotTreated, BuiltinArgument::Required, "NUM", ""},
            {'w', "word-regexp", kWordRegexp, BuiltinArgument::None, "",
                "only matches at word boundaries match"},
            {0, "auto-hybrid-regex", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "no-auto-hybrid-regex", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {0, "no-pcre2-unicode", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "pcre2-unicode", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            // Filter options.
            {0, "binary", kBinary, BuiltinArgument::None, "",
                "search binary files, reporting the first match"},
            {0, "no-binary", kNoBinary, BuiltinArgument::None, "", "", true},
            {'L', "follow", kFollow, BuiltinArgument::None, "",
                "accepted: HaisosOS follows the disk's links itself"},
            {0, "no-follow", kNoFollow, BuiltinArgument::None, "", "", true},
            {'g', "glob", kGlob, BuiltinArgument::Required, "GLOB",
                "include or exclude files by a glob ('!' excludes)"},
            {0, "glob-case-insensitive", kGlobCaseInsensitive, BuiltinArgument::None, "",
                "globs match insensitively"},
            {0, "no-glob-case-insensitive", kNoGlobCaseInsensitive, BuiltinArgument::None,
                "", "", true},
            {'.', "hidden", kHidden, BuiltinArgument::None, "",
                "search hidden files and directories"},
            {0, "no-hidden", kNoHidden, BuiltinArgument::None, "", "", true},
            {0, "iglob", kIglob, BuiltinArgument::Required, "GLOB",
                "a glob matched insensitively"},
            {0, "ignore-file", kBuiltinNotTreated, BuiltinArgument::Required, "PATH", ""},
            {0, "ignore-file-case-insensitive", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "no-ignore-file-case-insensitive", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {'d', "max-depth", kMaxDepth, BuiltinArgument::Required, "NUM",
                "descend at most NUM levels below each operand"},
            {0, "maxdepth", kMaxDepth, BuiltinArgument::Required, "NUM", "", true},
            {0, "max-filesize", kBuiltinNotTreated, BuiltinArgument::Required, "NUM", ""},
            {0, "no-ignore", kNoIgnore, BuiltinArgument::None, "",
                "respect no ignore file at all"},
            {0, "ignore", kIgnore, BuiltinArgument::None, "", "", true},
            {0, "no-ignore-dot", kNoIgnoreDot, BuiltinArgument::None, "",
                ".ignore and .rgignore not respected"},
            {0, "ignore-dot", kIgnoreDot, BuiltinArgument::None, "", "", true},
            {0, "no-ignore-exclude", kNoIgnoreExclude, BuiltinArgument::None, "",
                ".git/info/exclude not respected"},
            {0, "ignore-exclude", kIgnoreExclude, BuiltinArgument::None, "", "", true},
            {0, "no-ignore-files", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "ignore-files", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {0, "no-ignore-global", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "ignore-global", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {0, "no-ignore-parent", kNoIgnoreParent, BuiltinArgument::None, "",
                "no ignore file above the operand respected"},
            {0, "ignore-parent", kIgnoreParent, BuiltinArgument::None, "", "", true},
            {0, "no-ignore-vcs", kNoIgnoreVcs, BuiltinArgument::None, "",
                ".gitignore and .git/info/exclude not respected"},
            {0, "ignore-vcs", kIgnoreVcs, BuiltinArgument::None, "", "", true},
            {0, "no-require-git", kNoRequireGit, BuiltinArgument::None, "",
                "respect .gitignore outside a git repository"},
            {0, "require-git", kRequireGit, BuiltinArgument::None, "", "", true},
            {0, "one-file-system", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "no-one-file-system", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {'t', "type", kType, BuiltinArgument::Required, "TYPE",
                "only search files of a type (--type-list shows them)"},
            {'T', "type-not", kTypeNot, BuiltinArgument::Required, "TYPE",
                "skip files of a type"},
            {0, "type-add", kBuiltinNotTreated, BuiltinArgument::Required, "TYPESPEC", ""},
            {0, "type-clear", kBuiltinNotTreated, BuiltinArgument::Required, "TYPE", ""},
            {0, "type-list", kTypeList, BuiltinArgument::None, "",
                "print the known file types and exit"},
            {'u', "unrestricted", kUnrestricted, BuiltinArgument::None, "",
                "relax the filters: -u ignore files, -uu hidden, -uuu binary"},
            // Output options.
            {'A', "after-context", kAfterContext, BuiltinArgument::Required, "NUM",
                "show NUM lines after each match"},
            {'B', "before-context", kBeforeContext, BuiltinArgument::Required, "NUM",
                "show NUM lines before each match"},
            {0, "block-buffered", kBlockBuffered, BuiltinArgument::None, "",
                "do not flush before the output buffer is full"},
            {0, "no-block-buffered", kNoBlockBuffered, BuiltinArgument::None, "", "", true},
            {'b', "byte-offset", kByteOffset, BuiltinArgument::None, "",
                "print the byte offset of each output line"},
            {0, "no-byte-offset", kNoByteOffset, BuiltinArgument::None, "", "", true},
            {0, "color", kColor, BuiltinArgument::Required, "WHEN",
                "when to colour: never, auto, always or ansi"},
            {0, "colors", kBuiltinNotTreated, BuiltinArgument::Required, "COLOR_SPEC", ""},
            {0, "column", kColumn, BuiltinArgument::None, "",
                "print the column of the first match of each line"},
            {0, "no-column", kNoColumn, BuiltinArgument::None, "", "", true},
            {'C', "context", kContext, BuiltinArgument::Required, "NUM",
                "show NUM lines before and after each match"},
            {0, "context-separator", kContextSeparator, BuiltinArgument::Required, "SEPARATOR",
                "the separator between context groups (default --)"},
            {0, "no-context-separator", kNoContextSeparator, BuiltinArgument::None, "", "", true},
            {0, "field-context-separator", kBuiltinNotTreated, BuiltinArgument::Required, "SEP", ""},
            {0, "field-match-separator", kBuiltinNotTreated, BuiltinArgument::Required, "SEP", ""},
            {0, "heading", kHeading, BuiltinArgument::None, "",
                "print each file's path above its lines"},
            {0, "no-heading", kNoHeading, BuiltinArgument::None, "", "", true},
            {'h', "help", kBuiltinOptionHelp, BuiltinArgument::None, "", "show this help"},
            {0, "hostname-bin", kBuiltinNotTreated, BuiltinArgument::Required, "COMMAND", ""},
            {0, "hyperlink-format", kBuiltinNotTreated, BuiltinArgument::Required, "FORMAT", ""},
            {0, "include-zero", kIncludeZero, BuiltinArgument::None, "",
                "with the counts, print zero counts too"},
            {0, "no-include-zero", kNoIncludeZero, BuiltinArgument::None, "", "", true},
            {0, "line-buffered", kLineBuffered, BuiltinArgument::None, "",
                "flush the output on every line"},
            {0, "no-line-buffered", kNoLineBuffered, BuiltinArgument::None, "", "", true},
            {'n', "line-number", kLineNumber, BuiltinArgument::None, "", "show line numbers"},
            {'N', "no-line-number", kNoLineNumber, BuiltinArgument::None, "",
                "hide line numbers"},
            {'M', "max-columns", kMaxColumns, BuiltinArgument::Required, "NUM",
                "omit lines at least NUM bytes long (0: none)"},
            {0, "max-columns-preview", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "no-max-columns-preview", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {'0', "null", kNull, BuiltinArgument::None, "",
                "end printed paths with a NUL byte"},
            {'o', "only-matching", kOnlyMatching, BuiltinArgument::None, "",
                "print each match on a line of its own"},
            {0, "path-separator", kBuiltinNotTreated, BuiltinArgument::Required, "SEP", ""},
            {0, "passthru", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "passthrough", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {'p', "pretty", kPretty, BuiltinArgument::None, "",
                "--color=always --heading --line-number"},
            {'q', "quiet", kQuiet, BuiltinArgument::None, "",
                "print nothing; stop at the first match"},
            {'r', "replace", kBuiltinNotTreated, BuiltinArgument::Required, "TEXT", ""},
            {0, "sort", kSort, BuiltinArgument::Required, "SORTBY",
                "sort the results: path (the order used) or none"},
            {0, "sortr", kBuiltinNotTreated, BuiltinArgument::Required, "SORTBY", ""},
            {0, "trim", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "no-trim", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {0, "vimgrep", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {'H', "with-filename", kWithFilename, BuiltinArgument::None, "",
                "print the path with each line"},
            {'I', "no-filename", kNoFilename, BuiltinArgument::None, "",
                "never print the path"},
            {0, "sort-files", kSortFiles, BuiltinArgument::None, "",
                "(deprecated) sort the results by path"},
            {0, "no-sort-files", kNoSortFiles, BuiltinArgument::None, "", "", true},
            // Output modes.
            {'c', "count", kCount, BuiltinArgument::None, "",
                "print the number of matching lines per file"},
            {0, "count-matches", kCountMatches, BuiltinArgument::None, "",
                "print the number of matches per file"},
            {'l', "files-with-matches", kFilesWithMatches, BuiltinArgument::None, "",
                "print only the paths with a match"},
            {0, "files-without-match", kFilesWithoutMatch, BuiltinArgument::None, "",
                "print only the paths without a match"},
            {0, "json", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "no-json", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            // Logging options.
            {0, "debug", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "no-ignore-messages", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "ignore-messages", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {0, "no-messages", kNoMessages, BuiltinArgument::None, "",
                "suppress some error messages"},
            {0, "messages", kMessages, BuiltinArgument::None, "", "", true},
            {0, "stats", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "no-stats", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {0, "trace", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            // Other behaviors.
            {0, "files", kFiles, BuiltinArgument::None, "",
                "print each file that would be searched"},
            {0, "generate", kBuiltinNotTreated, BuiltinArgument::Required, "KIND", ""},
            {0, "no-config", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "pcre2-version", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {'V', "version", kBuiltinOptionVersion, BuiltinArgument::None, "",
                "show the version"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        BuiltinHelp help;
        help.summary = "recursively search the current directory for lines matching a pattern";
        help.usage = {
            "rg [OPTIONS] PATTERN [PATH ...]",
            "rg [OPTIONS] -e PATTERN ... [PATH ...]",
            "rg [OPTIONS] --files [PATH ...]",
        };
        help.notes =
            "Paths are searched one at a time, in operand order, and each\n"
            "directory's entries in byte order (rg's --sort path order; rg itself\n"
            "searches in parallel, so its own order varies). Long flags may be\n"
            "abbreviated (rg wants them whole); rg's \"similar flags\" hint is not\n"
            "printed. Patterns are read as Rust's regex syntax, onto Haisos's\n"
            "byte-wise Perl subset: \\w, . and -i work on bytes, not Unicode;\n"
            "\\<, \\> and \\b{...} are \\b; \\p{...} classes, class set operations\n"
            "and nested classes are refused, with rg's parse-error frame.\n"
            "The walk skips what ripgrep skips: hidden names, what .gitignore\n"
            "(inside a git repository), .git/info/exclude, .ignore and .rgignore\n"
            "say, and the -g globs and -t types decide -- operands are never\n"
            "filtered. The file types are a subset of ripgrep's own\n"
            "(--type-list shows it); no global gitignore (core.excludesFile) is\n"
            "read; -L is accepted and changes nothing, for HaisosOS's\n"
            "filesystems follow the disk's links themselves. With no\n"
            "PATH, the standard input is searched only when it is not a terminal\n"
            "and its first read returns data: an input that is empty at once --\n"
            "/dev/null's case, for a descriptor has no type in HaisosOS -- means\n"
            ". is searched instead. A binary file named as an operand (or with\n"
            "--binary, or the standard input) prints rg's message at its first\n"
            "match and ends there; a binary file met while walking ends\n"
            "silently. Exit status is 0 when a line matched, 1 when none did,\n"
            "2 on trouble.";
        help.referenceUrl = "https://github.com/BurntSushi/ripgrep/blob/14.1.1/GUIDE.md";
        return help;
    }

    int Run(BuiltinContext& context) override {
        ParsedBuiltinArgs parsed = ParseBuiltinArgs(context.Args(), Options());
        if (!parsed.error.empty()) {
            TranslateParseError(context, context.Args(), parsed.error);
            return 2;
        }
        // --help and --version win over everything else given, whichever
        // comes first.
        for (const auto& option : parsed.options) {
            if (option.id == kBuiltinOptionHelp) {
                context.Out(BuiltinHelpText(*this));
                return 0;
            }
            if (option.id == kBuiltinOptionVersion) {
                context.Out(BuiltinVersionText(*this));
                return 0;
            }
        }
        context.ReportNotTreated(parsed);

        RgSettings settings;
        std::vector<std::string> patterns;
        bool patternsGiven = false;  // -e or -f: every operand is a path
        // -s/-i/-S, the last one given winning.
        int caseFlag = 0;
        bool fixedStrings = false;
        bool wordRegexp = false;
        bool lineRegexp = false;
        std::optional<uint64_t> afterGiven;
        std::optional<uint64_t> beforeGiven;
        std::optional<uint64_t> contextGiven;
        int unrestricted = 0;  // -u, counted
        bool globFold = false;  // --glob-case-insensitive

        for (const auto& option : parsed.options) {
            switch (option.id) {
                case kPatterns:
                    patterns.push_back(option.argument);
                    patternsGiven = true;
                    break;
                case kPatternFile: {
                    patternsGiven = true;
                    const std::string& file = option.argument;
                    InputOpenFailure failure = InputOpenFailure::None;
                    std::shared_ptr<IFileDescriptor> input;
                    if (file == "-") {
                        input = context.IO().GetDescriptor(IFileIO::kStdIn);
                        if (!input) {
                            failure = InputOpenFailure::BadDescriptor;
                        }
                    } else {
                        input = OpenInputOperand(context, file, failure);
                    }
                    if (!input) {
                        context.ErrorText("rg: " + file + ": " + OpenFailureText(failure)
                            + " (os error " + std::to_string(OpenFailureErrno(failure)) + ")\n");
                        return 2;
                    }
                    BuiltinLineReader reader(context, *input, '\n');
                    while (true) {
                        std::string line;
                        bool delimited = false;
                        const LineReadResult result = reader.Next(line, delimited);
                        if (result == LineReadResult::End) {
                            break;
                        }
                        if (result != LineReadResult::Line) {
                            // A stop asked for, or a read that failed.
                            return result == LineReadResult::Stopped ? 1 : 2;
                        }
                        patterns.push_back(line);
                    }
                    break;
                }
                case kCaseSensitive: case kIgnoreCase: case kSmartCase:
                    caseFlag = option.id;
                    break;
                case kFixedStrings: fixedStrings = true; break;
                case kNoFixedStrings: fixedStrings = false; break;
                case kInvert: settings.invert = true; break;
                case kNoInvert: settings.invert = false; break;
                case kLineRegexp:
                    lineRegexp = true;
                    wordRegexp = false;
                    break;
                case kWordRegexp:
                    wordRegexp = true;
                    lineRegexp = false;
                    break;
                case kMaxCount: {
                    uint64_t value = 0;
                    if (!ParseNumberFlag(context, option.argument, option.spelling, value)) {
                        return 2;
                    }
                    settings.maxCount = value;
                    break;
                }
                case kText: settings.text = true; break;
                case kNoText: settings.text = false; break;
                case kBinary: settings.binary = true; break;
                case kNoBinary: settings.binary = false; break;
                case kAfterContext: {
                    uint64_t value = 0;
                    if (!ParseNumberFlag(context, option.argument, option.spelling, value)) {
                        return 2;
                    }
                    afterGiven = value;
                    break;
                }
                case kBeforeContext: {
                    uint64_t value = 0;
                    if (!ParseNumberFlag(context, option.argument, option.spelling, value)) {
                        return 2;
                    }
                    beforeGiven = value;
                    break;
                }
                case kContext: {
                    uint64_t value = 0;
                    if (!ParseNumberFlag(context, option.argument, option.spelling, value)) {
                        return 2;
                    }
                    contextGiven = value;
                    break;
                }
                case kByteOffset: settings.byteOffsets = true; break;
                case kNoByteOffset: settings.byteOffsets = false; break;
                case kColumn: settings.columns = true; break;
                case kNoColumn: settings.columns = false; break;
                case kColor: {
                    const std::string& value = option.argument;
                    if (value == "never") {
                        settings.color = 0;
                    } else if (value == "auto") {
                        settings.color = 1;
                    } else if (value == "always" || value == "ansi") {
                        settings.color = 2;
                    } else {
                        context.ErrorText("rg: error parsing flag --color: choice '" + value
                            + "' is unrecognized\n");
                        return 2;
                    }
                    break;
                }
                case kContextSeparator:
                    settings.separator = option.argument;
                    settings.contextSeparator = true;
                    break;
                case kNoContextSeparator: settings.contextSeparator = false; break;
                case kHeading: settings.heading = true; break;
                case kNoHeading: settings.heading = false; break;
                case kLineNumber: settings.lineNumbers = true; break;
                case kNoLineNumber: settings.lineNumbers = false; break;
                case kMaxColumns: {
                    uint64_t value = 0;
                    if (!ParseNumberFlag(context, option.argument, option.spelling, value)) {
                        return 2;
                    }
                    settings.maxColumns = value > 0
                        ? std::optional<uint64_t>(value) : std::optional<uint64_t>();
                    break;
                }
                case kNull: settings.nullSeparator = true; break;
                case kOnlyMatching: settings.onlyMatching = true; break;
                case kPretty:
                    settings.color = 2;
                    settings.heading = true;
                    settings.lineNumbers = true;
                    break;
                case kQuiet: settings.mode = RgMode::Quiet; break;
                case kWithFilename: settings.withFilename = true; break;
                case kNoFilename: settings.withFilename = false; break;
                case kCount: settings.mode = RgMode::Count; break;
                case kCountMatches: settings.mode = RgMode::CountMatches; break;
                case kFilesWithMatches: settings.mode = RgMode::ListMatching; break;
                case kFilesWithoutMatch: settings.mode = RgMode::ListNonMatching; break;
                case kIncludeZero: settings.includeZero = true; break;
                case kNoIncludeZero: settings.includeZero = false; break;
                case kLineBuffered: settings.lineBuffered = true; break;
                case kNoLineBuffered: settings.lineBuffered = false; break;
                case kBlockBuffered: settings.lineBuffered = false; break;
                case kNoBlockBuffered: break;  // rg's default off a terminal
                case kNoMessages: settings.noMessages = true; break;
                case kMessages: settings.noMessages = false; break;
                case kFiles: settings.mode = RgMode::Files; break;
                case kSort:
                    // path is the order the walk already takes; the other
                    // values rg takes by metadata are not acted on.
                    if (option.argument != "path" && option.argument != "none") {
                        context.NotTreated("--sort=" + option.argument);
                    }
                    break;
                case kSortFiles: case kNoSortFiles: break;  // the walk is in path order
                case kFollow: case kNoFollow: break;  // accepted: HaisosOS follows links
                case kGlob: settings.globs.push_back({option.argument, false}); break;
                case kIglob: settings.globs.push_back({option.argument, true}); break;
                case kGlobCaseInsensitive: globFold = true; break;
                case kNoGlobCaseInsensitive: globFold = false; break;
                case kHidden: settings.hidden = true; break;
                case kNoHidden: settings.hidden = false; break;
                case kMaxDepth: {
                    uint64_t value = 0;
                    if (!ParseNumberFlag(context, option.argument, option.spelling, value)) {
                        return 2;
                    }
                    settings.maxDepth = value;
                    break;
                }
                case kNoIgnore:
                    settings.noIgnoreDot = true;
                    settings.noIgnoreVcs = true;
                    settings.noIgnoreExclude = true;
                    break;
                case kIgnore:
                    settings.noIgnoreDot = false;
                    settings.noIgnoreVcs = false;
                    settings.noIgnoreExclude = false;
                    break;
                case kNoIgnoreDot: settings.noIgnoreDot = true; break;
                case kIgnoreDot: settings.noIgnoreDot = false; break;
                case kNoIgnoreExclude: settings.noIgnoreExclude = true; break;
                case kIgnoreExclude: settings.noIgnoreExclude = false; break;
                case kNoIgnoreParent: settings.noIgnoreParent = true; break;
                case kIgnoreParent: settings.noIgnoreParent = false; break;
                case kNoIgnoreVcs: settings.noIgnoreVcs = true; break;
                case kIgnoreVcs: settings.noIgnoreVcs = false; break;
                case kNoRequireGit: settings.requireGit = false; break;
                case kRequireGit: settings.requireGit = true; break;
                case kType:
                case kTypeNot: {
                    const std::vector<std::string_view>* typeGlobs =
                        FindRgTypeGlobs(option.argument);
                    if (!typeGlobs) {
                        context.ErrorText("rg: unrecognized file type: " + option.argument + "\n");
                        return 2;
                    }
                    std::vector<std::string>& into =
                        option.id == kType ? settings.typeSelected : settings.typeNegated;
                    for (const auto& glob : *typeGlobs) {
                        into.emplace_back(glob);
                    }
                    break;
                }
                case kTypeList:
                    context.Out(RgTypeListText());
                    return 0;
                case kUnrestricted: ++unrestricted; break;
                default: break;
            }
        }

        // -u, counted: 1 = --no-ignore, 2 = also --hidden, 3 = also --binary.
        if (unrestricted > 0) {
            settings.noIgnoreDot = true;
            settings.noIgnoreVcs = true;
            settings.noIgnoreExclude = true;
        }
        if (unrestricted > 1) {
            settings.hidden = true;
        }
        if (unrestricted > 2) {
            settings.binary = true;
        }
        // --glob-case-insensitive folds every -g glob (--iglob's own always).
        if (globFold) {
            for (auto& glob : settings.globs) {
                glob.caseFold = true;
            }
        }

        // -A/-B win over -C for their side, whatever the order.
        if (contextGiven) {
            settings.after = *contextGiven;
            settings.before = *contextGiven;
            settings.contextEnabled = true;
        }
        if (afterGiven) {
            settings.after = *afterGiven;
            settings.contextEnabled = true;
        }
        if (beforeGiven) {
            settings.before = *beforeGiven;
            settings.contextEnabled = true;
        }
        // --column implies line numbers, unless -n or -N was given.
        if (settings.columns && !settings.lineNumbers.has_value()) {
            settings.lineNumbers = true;
        }
        // -c -o counts matches, as rg does.
        if (settings.mode == RgMode::Count && settings.onlyMatching) {
            settings.mode = RgMode::CountMatches;
        }

        // The pattern: the first operand, unless -e/-f or --files made every
        // operand a path.
        std::vector<std::string> paths = parsed.operands;
        if (!patternsGiven && settings.mode != RgMode::Files) {
            if (paths.empty()) {
                context.ErrorText("rg: ripgrep requires at least one pattern to execute a search\n");
                return 2;
            }
            patterns.push_back(paths.front());
            paths.erase(paths.begin());
        }

        for (const auto& pattern : patterns) {
            if (pattern.find('\n') != std::string::npos) {
                MultilinePatternError(context);
                return 2;
            }
        }

        GrepMatcherOptions matcherOptions;
        matcherOptions.syntax = GrepSyntax::Perl;
        if (wordRegexp) {
            matcherOptions.wholeWord = GrepWholeWord::NonWordNeighbours;
        }
        matcherOptions.wholeLine = lineRegexp;
        if (fixedStrings) {
            for (auto& pattern : patterns) {
                pattern = EscapeLiteral(pattern);
            }
        }
        // The patterns as Rust's regex syntax, onto the Perl subset: rg's
        // wrapped pattern, or its parse-error frame. -S looks at the parsed
        // literals -- an uppercase letter spelled as an escape does not turn
        // case sensitivity on.
        std::string wrapped;
        RgRegexError regexError;
        bool hasUppercaseLiteral = false;
        std::optional<std::string> perlPattern;
        if (!patterns.empty()) {
            perlPattern = TranslateRgPattern(patterns, wrapped, regexError, hasUppercaseLiteral);
            if (!perlPattern) {
                if (regexError.multiline) {
                    MultilinePatternError(context);
                } else {
                    context.ErrorText(FormatRgRegexError(wrapped, regexError));
                }
                return 2;
            }
        }
        matcherOptions.ignoreCase = caseFlag == kIgnoreCase
            || (caseFlag == kSmartCase && !hasUppercaseLiteral);
        std::string matcherError;
        std::shared_ptr<const GrepMatcher> matcher = GrepMatcher::Create(
            patterns.empty() ? std::vector<std::string>() : std::vector<std::string>{*perlPattern},
            matcherOptions, matcherError);
        if (!matcher) {
            // TranslateRgPattern pre-compiles its result, so this is only a
            // wrapper Regex adds (-x's anchors): the frame rg would still
            // print, with Regex's own message.
            regexError = RgRegexError{};
            regexError.message = matcherError;
            context.ErrorText(FormatRgRegexError(wrapped, regexError));
            return 2;
        }

        const RgResult result = RgSearch(context, settings, *matcher, paths);
        if (result.stopped) {
            if (settings.mode == RgMode::Quiet && result.quietMatched) {
                return 0;
            }
            return 1;
        }
        if (settings.mode == RgMode::Quiet && result.matched) {
            return 0;
        }
        if (result.error || result.noFilesSearched) {
            return 2;
        }
        return result.matched ? 0 : 1;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateRgCommand() {
    return std::make_shared<RgCommand>();
}

} // namespace Haisos