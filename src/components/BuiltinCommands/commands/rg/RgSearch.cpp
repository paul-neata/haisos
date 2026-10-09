#include "commands/rg/RgSearch.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include "BuiltinFnmatch.h"
#include "BuiltinText.h"
#include "commands/grep/GrepContext.h"
#include "commands/rg/RgIgnore.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "interfaces/IFileIO.h"
#include "interfaces/IProcess.h"

namespace Haisos {
namespace {

// ripgrep's read buffer.
constexpr size_t kRgReadChunk = 65536;

// rg's colours, byte for byte: each piece wrapped in its own SGR run, reset
// with ESC [ 0 m before it, as ripgrep's printer does.
std::string ColorPath(const RgSettings& settings, const std::string& text) {
    return settings.color == 2 ? "\x1b[0m\x1b[35m" + text + "\x1b[0m" : text;
}

std::string ColorLineNumber(const RgSettings& settings, const std::string& text) {
    return settings.color == 2 ? "\x1b[0m\x1b[32m" + text + "\x1b[0m" : text;
}

std::string ColorNumber(const RgSettings& settings, const std::string& text) {
    return settings.color == 2 ? "\x1b[0m" + text + "\x1b[0m" : text;
}

std::string ColorMatch(const RgSettings& settings, std::string_view text) {
    return settings.color == 2
        ? "\x1b[0m\x1b[1m\x1b[31m" + std::string(text) + "\x1b[0m"
        : std::string(text);
}

// A matching line with every match highlighted, as ripgrep prints it. A
// context line is never highlighted (with -v the selected lines hold no
// matches, so a selected one always does).
std::string Highlighted(const RgSettings& settings, const GrepMatcher& matcher,
                        std::string_view text) {
    if (settings.color != 2) {
        return std::string(text);
    }
    std::string out;
    size_t pos = 0;
    size_t lastEnd = 0;
    while (pos <= text.size()) {
        size_t begin = 0;
        size_t end = 0;
        if (!matcher.Find(text, pos, begin, end)) {
            break;
        }
        if (end > begin) {
            out.append(text.substr(lastEnd, begin - lastEnd));
            out += ColorMatch(settings, text.substr(begin, end - begin));
            lastEnd = end;
            pos = end;
        } else {
            pos = begin + 1;
        }
    }
    out.append(text.substr(lastEnd));
    return out;
}

// Everything rg prints while searching: the prefixes, the headings, the
// group separators, the counts and paths, the binary-file message. One
// instance spans the run; each input begins with its own name.
class RgPrinter {
public:
    RgPrinter(BuiltinContext& context, const RgSettings& settings, const GrepMatcher& matcher,
              bool showNames, bool heading, bool lineNumbers)
        : m_context(context)
        , m_settings(settings)
        , m_matcher(matcher)
        , m_showNames(showNames)
        , m_heading(heading)
        , m_lineNumbers(lineNumbers) {}

    const std::string& Name() const { return m_name; }

    void BeginFile(const std::string& name) {
        m_name = name;
        m_headingDone = false;
    }

    // One output line. |selected|: a matching line (':' after each field);
    // a context line's separators are '-'. |matchText|: the line is one
    // match of -o, starting |matchBegin| bytes into its line, for
    // --column and -b.
    void Line(std::string_view text, uint64_t lineNumber, uint64_t byteOffset, bool selected,
              bool matchText, size_t matchBegin) {
        if (m_heading && !m_headingDone) {
            if (m_anythingPrinted) {
                Out("\n");
            }
            Out(ColorPath(m_settings, m_name) + "\n");
            m_headingDone = true;
        }
        const char sep = selected ? ':' : '-';
        std::string out;
        if (!m_heading && m_showNames) {
            out += ColorPath(m_settings, m_name);
            out += m_settings.nullSeparator ? '\0' : sep;
        }
        if (m_lineNumbers) {
            out += ColorLineNumber(m_settings, std::to_string(lineNumber)) + sep;
        }
        if (m_settings.columns && selected && !m_settings.invert) {
            // The column of the line's first match (of the one match -o
            // prints); -v selects lines without matches, so there is none.
            const size_t begin = matchText ? matchBegin : FirstMatchBegin(text);
            out += ColorNumber(m_settings, std::to_string(begin + 1)) + sep;
        }
        if (m_settings.byteOffsets) {
            // With -o, the offset of the match, not of its line.
            out += ColorNumber(m_settings, std::to_string(byteOffset)) + sep;
        }
        // -M: a line at least as long as the limit prints in its place.
        if (m_settings.maxColumns && text.size() >= *m_settings.maxColumns) {
            out += selected ? "[Omitted long matching line]" : "[Omitted long context line]";
        } else if (matchText) {
            out += ColorMatch(m_settings, text);
        } else {
            out += Highlighted(m_settings, m_matcher, text);
        }
        out += '\n';
        Out(out);
    }

    // The separator between non-contiguous context groups ("--"), never
    // coloured, whatever else is.
    void Separator() {
        if (!m_settings.contextSeparator) {
            return;
        }
        Out(m_settings.separator + "\n");
    }

    // [path:]N, the count of a whole file (-c, --count-matches).
    void Count(const std::string& name, uint64_t count) {
        std::string out;
        if (m_showNames) {
            out += ColorPath(m_settings, name);
            out += m_settings.nullSeparator ? '\0' : ':';
        }
        out += std::to_string(count);
        out += '\n';
        Out(out);
    }

    // A path on a line of its own (-l, --files-without-match, --files).
    void Path(const std::string& name) {
        Out(ColorPath(m_settings, name) + std::string(1, m_settings.nullSeparator ? '\0' : '\n'));
    }

    // What a binary input says when its first match ends its search. Always
    // in the path: prefix form, headings included; the blank line a heading
    // would start with still comes first.
    void BinaryMessage(uint64_t nulOffset) {
        if (m_heading && !m_headingDone) {
            if (m_anythingPrinted) {
                Out("\n");
            }
            m_headingDone = true;
        }
        std::string out;
        if (m_showNames) {
            out += m_name;
            out += m_settings.nullSeparator ? '\0' : ':';
            out += ' ';
        }
        out += "binary file matches (found \"\\0\" byte around offset "
            + std::to_string(nulOffset) + ")\n";
        Out(out);
    }

private:
    size_t FirstMatchBegin(std::string_view text) {
        size_t begin = 0;
        size_t end = 0;
        if (m_matcher.Find(text, 0, begin, end)) {
            return begin;
        }
        return 0;
    }

    void Out(const std::string& text) {
        m_context.Out(text);
        if (m_settings.lineBuffered) {
            m_context.Flush();
        }
        m_anythingPrinted = true;
    }

    BuiltinContext& m_context;
    const RgSettings& m_settings;
    const GrepMatcher& m_matcher;
    bool m_showNames;
    bool m_heading;
    bool m_lineNumbers;
    std::string m_name;
    bool m_headingDone = false;
    bool m_anythingPrinted = false;
};

// What one searched input came to.
struct FileOutcome {
    uint64_t selected = 0;  // matching lines
    uint64_t matches = 0;   // matches (--count-matches)
    bool binaryMatched = false;
    bool stopped = false;   // the input's search ended early (a match, a stop)
    bool error = false;
};

// Searches one input: ripgrep's chunked reader (a carry, line numbers, byte
// offsets), its binary rules and its -m rules. |binaryAsOperand|: a NUL
// makes the input binary (an operand, stdin, or --binary) instead of ending
// it silently (a file met while walking). |initialData|: bytes already read
// (the one read that decided standard input would be searched).
FileOutcome SearchFile(BuiltinContext& context, const RgSettings& settings, const GrepMatcher& matcher,
                       IFileDescriptor& input, RgPrinter& printer, GrepContext* grepContext,
                       const std::string& initialData, bool binaryAsOperand) {
    FileOutcome result;
    const bool counting = settings.mode == RgMode::Count || settings.mode == RgMode::CountMatches;
    const bool listing = settings.mode == RgMode::ListMatching || settings.mode == RgMode::ListNonMatching;

    uint64_t lineNo = 0;
    uint64_t offset = 0;  // the byte offset of the next line's first byte
    bool binary = false;
    std::optional<uint64_t> firstNul;  // the first NUL of a binary input
    bool binaryMatched = false;
    bool limitReached = false;  // -m used up, trailing context still owed

    // One line (without its terminator). Returns whether to stop reading.
    const auto processLine = [&](std::string_view line) -> bool {
        ++lineNo;
        if (limitReached) {
            // -m with context: only as far as the trailing context reaches;
            // a matching line inside it prints as a match (rg, not grep).
            if (grepContext) {
                const bool selected = matcher.Matches(line) != settings.invert;
                grepContext->Line(line, lineNo, offset, selected, /*extendsAfter=*/false);
                return !grepContext->AfterPending();
            }
            return true;
        }
        const bool selected = matcher.Matches(line) != settings.invert;
        if (selected) {
            ++result.selected;
            if (settings.mode == RgMode::CountMatches) {
                // Every match of the line, not the line.
                size_t pos = 0;
                while (pos <= line.size()) {
                    size_t begin = 0;
                    size_t end = 0;
                    if (!matcher.Find(line, pos, begin, end)) {
                        break;
                    }
                    if (end > begin) {
                        ++result.matches;
                    }
                    pos = end > begin ? end : begin + 1;
                }
            }
            if (settings.mode == RgMode::Quiet) {
                result.stopped = true;
                return true;
            }
            if (binary) {
                // The first match ends a binary input, the counts and lists
                // excepted: they keep going to the end of the file.
                binaryMatched = true;
                if (!counting && !listing) {
                    return true;
                }
            }
            if (listing) {
                return true;
            }
            if (counting) {
                // The counted line counts towards -m too.
            } else if (grepContext) {
                grepContext->Line(line, lineNo, offset, true);
            } else if (settings.onlyMatching) {
                size_t pos = 0;
                while (pos <= line.size()) {
                    size_t begin = 0;
                    size_t end = 0;
                    if (!matcher.Find(line, pos, begin, end)) {
                        break;
                    }
                    if (end > begin) {
                        printer.Line(line.substr(begin, end - begin), lineNo, offset + begin,
                                     /*selected=*/true, /*matchText=*/true, begin);
                    }
                    pos = end > begin ? end : begin + 1;
                }
            } else {
                printer.Line(line, lineNo, offset, true, /*matchText=*/false, 0);
            }
            if (settings.maxCount && result.selected >= *settings.maxCount) {
                if (grepContext && grepContext->AfterPending()) {
                    limitReached = true;
                    return false;
                }
                return true;
            }
            return false;
        }
        // A binary input prints nothing further, its message excepted; it
        // joins no context either.
        if (grepContext && !binary) {
            grepContext->Line(line, lineNo, offset, false);
        }
        return false;
    };

    std::string data = initialData;  // the carry: the partial line so far
    bool eof = false;
    bool stopInput = false;  // processLine asked to stop: nothing more is taken
    // The bytes at the end of data not yet checked for a NUL: the standard
    // input's deciding read first, then each read.
    size_t unchecked = initialData.size();
    // -m 0: the input is not read at all.
    const bool readAtAll = !settings.maxCount || *settings.maxCount > 0;

    if (readAtAll) {
        std::vector<char> buffer(kRgReadChunk);
        while (true) {
            if (context.StopRequested()) {
                result.stopped = true;
                break;
            }
            const ssize_t n = input.Read(buffer.data(), buffer.size());
            if (n == kIOInterrupted) {
                result.stopped = true;
                break;
            }
            if (n < 0) {
                if (!settings.noMessages) {
                    context.ErrorText("rg: " + printer.Name() + ": IO error for operation on "
                        + printer.Name() + ": Input/output error (os error 5)\n");
                }
                result.error = true;
                break;
            }
            if (n == 0) {
                eof = true;
            } else {
                data.append(buffer.data(), static_cast<size_t>(n));
                unchecked += static_cast<size_t>(n);
            }
            if (!binary && !settings.text && unchecked > 0
                && std::memchr(data.data() + data.size() - unchecked, '\0', unchecked) != nullptr) {
                firstNul = offset + data.find('\0');
                if (!binaryAsOperand) {
                    // A binary file met while walking ends silently: lines
                    // already printed stay, nothing else comes.
                    data.clear();
                    break;
                }
                // An operand (or stdin, or --binary): from here a NUL
                // ends a line too, nothing is printed, and the first
                // match ends the input with ripgrep's message.
                binary = true;
            }
            unchecked = 0;
            // Split what is held into complete lines -- at the end of input
            // too, so a carry already read (the standard input's deciding
            // read) is split before its last line is taken as unterminated.
            // Once binary, a NUL ends a line too (for matching, counting
            // and line numbers).
            size_t pos = 0;
            while (pos < data.size()) {
                if (context.StopRequested()) {
                    result.stopped = true;
                    break;
                }
                size_t end = data.find('\n', pos);
                if (binary) {
                    const size_t nul = data.find('\0', pos);
                    if (nul != std::string::npos && (end == std::string::npos || nul < end)) {
                        end = nul;
                    }
                }
                if (end == std::string::npos) {
                    break;
                }
                stopInput = processLine(std::string_view(data).substr(pos, end - pos));
                offset += end - pos + 1;
                pos = end + 1;
                if (stopInput || result.stopped) {
                    break;
                }
            }
            if (!result.stopped) {
                data.erase(0, pos);
            }
            if (eof || stopInput || result.stopped) {
                break;
            }
        }
    }

    if (eof && !stopInput && !data.empty() && !result.stopped) {
        // A last line without its terminator is printed with one added.
        processLine(data);
    }

    // A binary operand (or --binary, or stdin) that matched says so, in the
    // modes that print lines; the counts and lists need no message.
    if (binaryMatched && settings.mode == RgMode::Lines) {
        printer.BinaryMessage(*firstNul);
    }
    result.binaryMatched = binaryMatched;
    return result;
}

// One directory's ignore files, read once and cached by its absolute path.
struct RgDirIgnores {
    std::string absDir;  // '/'-separated, absolute, no trailing '/'
    bool hasGit = false;  // an entry named .git, a directory or a file
    bool gitIsDir = false;
    RgGitignore rgignore;   // .rgignore
    RgGitignore dotIgnore;  // .ignore
    RgGitignore gitignore;  // .gitignore
    RgGitignore gitExclude;  // .git/info/exclude
};

// The directories an entry's decision looks at: D (the directory being
// walked) and every ancestor up to '/', deepest first, with the index of the
// nearest one holding a .git -- the root of the repository D is inside.
struct RgChainEntry {
    const RgDirIgnores* data;
    bool aboveOperand;  // an ancestor above the operand's own directory
};
struct RgChain {
    std::vector<RgChainEntry> dirs;  // [0] is D, the last is '/'
    int repoRoot = -1;               // index of the nearest .git holder, -1 none
};

// What the walk skips, ripgrep's order: the -g globs first, then the ignore
// files by kind, then the types, then hidden names. Operands are never
// filtered; only entries met while walking go through here.
class RgIgnoreFilters {
public:
    RgIgnoreFilters(BuiltinContext& context, const RgSettings& settings)
        : m_context(context)
        , m_settings(settings) {
        for (const auto& glob : settings.globs) {
            m_globs.AddLines(glob.pattern, glob.caseFold);
        }
    }

    // The chain for a directory operand: its own directory and everything
    // above, up to '/'.
    RgChain ChainFor(const std::string& absDir) {
        RgChain chain;
        std::string dir = absDir;
        while (true) {
            chain.dirs.push_back({&DataFor(dir), !chain.dirs.empty()});
            if (dir == "/") {
                break;
            }
            const size_t slash = dir.rfind('/');
            dir = slash == 0 ? "/" : dir.substr(0, slash);
        }
        chain.repoRoot = -1;
        for (size_t i = 0; i < chain.dirs.size(); ++i) {
            if (chain.dirs[i].data->hasGit) {
                chain.repoRoot = static_cast<int>(i);
                break;
            }
        }
        return chain;
    }

    // The chain for a subdirectory of |chain|: its own data in front, the
    // repository root one further away (or itself, when it holds a .git).
    RgChain Descend(const RgChain& chain, const std::string& absChildDir) {
        RgChain next;
        next.dirs.reserve(chain.dirs.size() + 1);
        const RgDirIgnores& data = DataFor(absChildDir);
        next.dirs.push_back({&data, false});
        for (const auto& entry : chain.dirs) {
            next.dirs.push_back(entry);
        }
        next.repoRoot = data.hasGit ? 0
            : chain.repoRoot >= 0 ? chain.repoRoot + 1 : -1;
        return next;
    }

    // Whether an entry met while walking is skipped. |absChild|: its
    // absolute path; |printedPath|: the path as printed (a glob is matched
    // against it, without a leading "./"); |name|: its own name.
    bool SkipEntry(const RgChain& chain, const std::string& absChild,
                   const std::string& printedPath, const std::string& name,
                   bool isDirectory) {
        // 1. Globs, matched against the path relative to the working
        //    directory. A plain glob that matched whitelists the entry
        //    (steps 2-4 skipped); a '!' glob that matched excludes it; a
        //    file matched by none is skipped when any plain glob exists.
        if (!m_globs.Empty()) {
            std::string_view globPath = printedPath;
            if (globPath.compare(0, 2, "./") == 0) {
                globPath.remove_prefix(2);
            }
            const RgMatch glob = m_globs.Match(globPath, isDirectory);
            if (glob == RgMatch::Whitelist) {
                return true;  // an '!' glob: excluded
            }
            if (glob == RgMatch::Ignore) {
                return false;  // a plain glob: whitelisted
            }
            if (!isDirectory && m_globs.HasWhitelist()) {
                return true;
            }
        }
        bool whitelisted = false;
        // 2. Ignore files, by kind in this precedence: .rgignore, .ignore,
        //    .gitignore, .git/info/exclude. Within a kind the deepest
        //    directory whose file has a matching pattern decides; the
        //    first kind with a decision wins.
        for (int kind = 0; kind < 4; ++kind) {
            RgMatch match = RgMatch::None;
            for (size_t i = 0; i < chain.dirs.size(); ++i) {
                const RgChainEntry& entry = chain.dirs[i];
                if (!KindApplies(kind, entry, chain.repoRoot, i)) {
                    continue;
                }
                const RgGitignore* file = KindFile(*entry.data, kind);
                if (!file) {
                    continue;
                }
                match = file->Match(RelativeTo(entry.data->absDir, absChild), isDirectory);
                if (match != RgMatch::None) {
                    break;
                }
            }
            if (match == RgMatch::Ignore) {
                return true;
            }
            if (match == RgMatch::Whitelist) {
                whitelisted = true;  // remembered for the hidden check
                break;
            }
        }
        // 3. Types, files only: a -T type wins over -t; a -t type must match.
        if (!isDirectory) {
            if (MatchesTypeGlobs(m_settings.typeNegated, name)) {
                return true;
            }
            if (!m_settings.typeSelected.empty()) {
                if (MatchesTypeGlobs(m_settings.typeSelected, name)) {
                    whitelisted = true;
                } else {
                    return true;
                }
            }
        }
        // 4. A hidden name, unless --hidden or whitelisted above.
        if (!name.empty() && name[0] == '.' && !m_settings.hidden && !whitelisted) {
            return true;
        }
        return false;
    }

private:
    // One directory's data, read through context.IO() once, cached by path.
    const RgDirIgnores& DataFor(const std::string& absDir) {
        const auto cached = m_cache.find(absDir);
        if (cached != m_cache.end()) {
            return cached->second;
        }
        RgDirIgnores data;
        data.absDir = absDir;
        for (const auto& entry : m_context.IO().ReadDirectory(absDir)) {
            if (entry.name == ".git") {
                data.hasGit = true;
                data.gitIsDir = entry.type == DirectoryEntryType::Dir;
            }
        }
        const auto join = [&](const char* below) {
            return data.absDir == "/" ? "/" + std::string(below)
                : data.absDir + "/" + below;
        };
        std::string text;
        if (ReadWholeFile(m_context.IO(), join(".rgignore"), text)) {
            data.rgignore = RgGitignore::Parse(text, false);
        }
        if (ReadWholeFile(m_context.IO(), join(".ignore"), text)) {
            data.dotIgnore = RgGitignore::Parse(text, false);
        }
        if (ReadWholeFile(m_context.IO(), join(".gitignore"), text)) {
            data.gitignore = RgGitignore::Parse(text, false);
        }
        if (data.gitIsDir
            && ReadWholeFile(m_context.IO(), join(".git/info/exclude"), text)) {
            data.gitExclude = RgGitignore::Parse(text, false);
        }
        return m_cache.emplace(absDir, std::move(data)).first->second;
    }

    // Whether the ignore files of |kind| apply from a chain entry.
    bool KindApplies(int kind, const RgChainEntry& entry, int repoRoot, size_t index) const {
        switch (kind) {
            case 0:  // .rgignore
            case 1:  // .ignore
                if (m_settings.noIgnoreDot) {
                    return false;
                }
                return !(m_settings.noIgnoreParent && entry.aboveOperand);
            case 2:  // .gitignore
                if (m_settings.noIgnoreVcs) {
                    return false;
                }
                if (repoRoot >= 0) {
                    return index <= static_cast<size_t>(repoRoot);
                }
                return !m_settings.requireGit;
            case 3:  // .git/info/exclude: the repository root's alone
                if (m_settings.noIgnoreVcs || m_settings.noIgnoreExclude) {
                    return false;
                }
                return repoRoot >= 0 && index == static_cast<size_t>(repoRoot);
        }
        return false;
    }

    static const RgGitignore* KindFile(const RgDirIgnores& data, int kind) {
        const RgGitignore* file = kind == 0 ? &data.rgignore
            : kind == 1 ? &data.dotIgnore
            : kind == 2 ? &data.gitignore : &data.gitExclude;
        return file->Empty() ? nullptr : file;
    }

    // |absChild| relative to |absDir| (an ancestor of it): no leading "./".
    static std::string_view RelativeTo(const std::string& absDir, const std::string& absChild) {
        if (absDir == "/") {
            return std::string_view(absChild).substr(1);
        }
        return std::string_view(absChild).substr(absDir.size() + 1);
    }

    static bool MatchesTypeGlobs(const std::vector<std::string>& globs, const std::string& name) {
        for (const auto& glob : globs) {
            if (FnMatch(glob, name, 0)) {
                return true;
            }
        }
        return false;
    }

    BuiltinContext& m_context;
    const RgSettings& m_settings;
    RgGitignore m_globs;  // the -g/--iglob globs
    std::map<std::string, RgDirIgnores> m_cache;
};

// |absDir| + '/' + |name|, with nothing between at the root.
std::string JoinAbs(const std::string& absDir, const std::string& name) {
    return absDir == "/" ? "/" + name : absDir + "/" + name;
}

} // namespace

RgResult RgSearch(BuiltinContext& context, const RgSettings& settings, const GrepMatcher& matcher,
                  const std::vector<std::string>& paths) {
    RgResult result;
    const bool outIsTerminal = context.OutIsTerminal();
    const std::shared_ptr<IEnvironment> environment = context.Process().GetEnvironment();

    // No paths given: standard input when it is not a terminal and its
    // first read returns data (a descriptor has no type in Haisos, so an
    // input that is empty at once -- /dev/null's case -- means no stdin:
    // documented), else the implicit path, `.`.
    bool stdinSearched = false;
    std::string stdinData;
    std::shared_ptr<IFileDescriptor> stdinDescriptor;
    if (paths.empty() && settings.mode != RgMode::Files) {
        stdinDescriptor = context.IO().GetDescriptor(IFileIO::kStdIn);
        if (stdinDescriptor && !stdinDescriptor->IsTerminal()) {
            std::vector<char> buffer(kRgReadChunk);
            const ssize_t n = stdinDescriptor->Read(buffer.data(), buffer.size());
            if (n > 0) {
                stdinSearched = true;
                stdinData.assign(buffer.data(), static_cast<size_t>(n));
            } else if (n == kIOInterrupted) {
                result.stopped = true;
                return result;
            }
        }
    }
    std::vector<std::string> effective = paths;
    bool implicitDot = false;
    if (paths.empty() && !stdinSearched) {
        effective = {"."};
        implicitDot = true;
    }

    // Names are shown unless exactly one operand is not a directory, or
    // standard input is searched (-H and -I override).
    bool showNames = true;
    if (settings.withFilename) {
        showNames = *settings.withFilename;
    } else if (stdinSearched) {
        showNames = false;
    } else if (effective.size() == 1) {
        FileStatus status;
        showNames = context.IO().Stat(effective[0], status) == 0
            && status.type == DirectoryEntryType::Dir;
    }
    const bool heading = settings.heading.value_or(outIsTerminal && showNames);
    const bool lineNumbers = settings.lineNumbers.value_or(outIsTerminal);
    // --color=auto: a terminal, TERM set and not dumb, NO_COLOR unset or empty.
    bool useColor = settings.color == 2;
    if (settings.color == 1 && outIsTerminal && environment) {
        const auto term = environment->GetVariable("TERM");
        if (term && *term != "dumb") {
            const auto noColor = environment->GetVariable("NO_COLOR");
            if (!noColor || noColor->empty()) {
                useColor = true;
            }
        }
    }
    RgSettings search = settings;
    search.color = useColor ? 2 : 0;
    RgPrinter printer(context, search, matcher, showNames, heading, lineNumbers);

    // The run's before/after context, one for the whole run. Headings have
    // no separators between files (the blank line and the path separate
    // them); -o prints no context lines, so it joins none.
    std::optional<GrepContext> grepContext;
    if (settings.contextEnabled && settings.mode == RgMode::Lines && !settings.onlyMatching) {
        grepContext.emplace(settings.before, settings.after, /*enabled=*/true,
                            /*separatorAcrossFiles=*/!heading, nullptr, nullptr);
    }
    GrepContext* const grepContextPtr = grepContext ? &*grepContext : nullptr;

    // Each input joins the run's context under its own name and numbering.
    const auto beginFile = [&](const std::string& name) {
        printer.BeginFile(name);
        if (grepContextPtr) {
            grepContextPtr->SetPrinters(
                [&](std::string_view text, uint64_t lineNumber, uint64_t byteOffset, bool selected) {
                    printer.Line(text, lineNumber, byteOffset, selected, /*matchText=*/false, 0);
                },
                [&]() { printer.Separator(); });
            grepContextPtr->BeginFile();
        }
    };

    uint64_t filesSearched = 0;
    bool stopped = false;
    bool quietMatched = false;

    // The counts and lists of one searched input, after its search.
    const auto finishFile = [&](const FileOutcome& outcome) {
        if (outcome.error) {
            result.error = true;
        }
        switch (settings.mode) {
            case RgMode::Count:
            case RgMode::CountMatches:
                if (outcome.selected > 0 || settings.includeZero) {
                    printer.Count(printer.Name(),
                        settings.mode == RgMode::CountMatches ? outcome.matches : outcome.selected);
                    result.matched = result.matched || outcome.selected > 0;
                }
                break;
            case RgMode::ListMatching:
                if (outcome.selected > 0) {
                    printer.Path(printer.Name());
                    result.matched = true;
                }
                break;
            case RgMode::ListNonMatching:
                if (outcome.selected == 0) {
                    printer.Path(printer.Name());
                    result.matched = true;
                }
                break;
            case RgMode::Lines:
            case RgMode::Quiet:
            case RgMode::Files:
                result.matched = result.matched || outcome.selected > 0;
                break;
        }
    };

    // One path to search: a file, a device, whatever is not a directory.
    // |operand|: named on the command line (a NUL makes it binary, and its
    // open failure, when it is the only one, is rg's longer message).
    const auto searchFile = [&](const std::string& file, bool operand) {
        InputOpenFailure failure = InputOpenFailure::None;
        auto input = OpenInputOperand(context, file, failure);
        if (!input) {
            if (!settings.noMessages) {
                const std::string text = std::string(OpenFailureText(failure)) + " (os error "
                    + std::to_string(OpenFailureErrno(failure)) + ")";
                if (operand && effective.size() == 1) {
                    context.ErrorText("rg: " + file + ": IO error for operation on " + file
                        + ": " + text + "\n");
                } else {
                    context.ErrorText("rg: " + file + ": " + text + "\n");
                }
            }
            result.error = true;
            return;
        }
        ++filesSearched;
        beginFile(file);
        FileOutcome outcome = SearchFile(context, search, matcher, *input, printer, grepContextPtr,
                                         /*initialData=*/std::string(), operand || settings.binary);
        if (outcome.stopped) {
            stopped = true;
            quietMatched = quietMatched || outcome.selected > 0;
        }
        finishFile(outcome);
    };

    // The operand as given: its children are it + '/' + their names, with
    // no '/' added when it already ends with one; the implicit `.` has no
    // prefix at all, so its entries print without `./`.
    const auto childPrefix = [&](const std::string& operand) {
        if (implicitDot && operand == ".") {
            return std::string();
        }
        if (!operand.empty() && operand.back() == '/') {
            return operand;
        }
        return operand + "/";
    };

    // What the walk skips: the -g globs, the ignore files, the types, hidden
    // names. Operands are never filtered; only entries met while walking.
    RgIgnoreFilters filters(context, settings);

    // The recursive walk, depth first: the entries of a directory minus
    // . and .., in byte order, files and directories together (rg's
    // --sort path order, without its parallelism). A device met while
    // walking is skipped, and so is what ripgrep skips: the filters decide
    // per entry, a skipped directory not descended. |depth|: the operand's
    // directory is 0, its entries 1; with --max-depth nothing deeper than
    // that is visited.
    const auto walk = [&](const auto& self, const std::string& dirPath,
                          const std::string& prefix, const RgChain& chain,
                          uint64_t depth) -> void {
        if (context.StopRequested() || stopped) {
            return;
        }
        if (settings.maxDepth && depth >= *settings.maxDepth) {
            return;
        }
        const std::string& absDir = chain.dirs[0].data->absDir;
        std::vector<DirectoryEntry> children;
        for (const auto& entry : context.IO().ReadDirectory(dirPath)) {
            if (entry.name != "." && entry.name != "..") {
                children.push_back(entry);
            }
        }
        std::sort(children.begin(), children.end(),
            [](const DirectoryEntry& a, const DirectoryEntry& b) { return a.name < b.name; });
        for (const auto& entry : children) {
            if (context.StopRequested() || stopped) {
                return;
            }
            const bool isDirectory = entry.type == DirectoryEntryType::Dir;
            const std::string child = prefix + entry.name;
            if (filters.SkipEntry(chain, JoinAbs(absDir, entry.name), child, entry.name,
                                  isDirectory)) {
                continue;
            }
            if (isDirectory) {
                self(self, child, child + "/",
                     filters.Descend(chain, JoinAbs(absDir, entry.name)), depth + 1);
            } else if (entry.type == DirectoryEntryType::CharDevice) {
                continue;
            } else if (settings.mode == RgMode::Files) {
                printer.Path(child);
                result.matched = true;
            } else {
                searchFile(child, /*operand=*/false);
            }
        }
    };

    if (stdinSearched) {
        ++filesSearched;
        beginFile("<stdin>");
        FileOutcome outcome = SearchFile(context, search, matcher, *stdinDescriptor, printer,
                                         grepContextPtr, stdinData, /*binaryAsOperand=*/true);
        if (outcome.stopped) {
            stopped = true;
            quietMatched = quietMatched || outcome.selected > 0;
        }
        finishFile(outcome);
    }

    for (const auto& path : effective) {
        if (context.StopRequested() || stopped) {
            break;
        }
        FileStatus status;
        const bool haveStat = context.IO().Stat(path, status) == 0;
        if (haveStat && status.type == DirectoryEntryType::Dir) {
            // A directory operand is itself never filtered; the walk inside
            // it decides per entry.
            walk(walk, path, childPrefix(path), filters.ChainFor(context.IO().ResolvePath(path)), 0);
            continue;
        }
        if (!haveStat) {
            // A path that is not there: rg's message, its longer form when
            // it is the only operand.
            if (!settings.noMessages) {
                if (effective.size() == 1) {
                    context.ErrorText("rg: " + path + ": IO error for operation on " + path
                        + ": No such file or directory (os error 2)\n");
                } else {
                    context.ErrorText("rg: " + path
                        + ": No such file or directory (os error 2)\n");
                }
            }
            result.error = true;
            continue;
        }
        if (settings.mode == RgMode::Files) {
            // --files lists every operand that would be searched, too.
            printer.Path(path);
            result.matched = true;
            continue;
        }
        searchFile(path, /*operand=*/true);
    }

    if (stopped) {
        result.stopped = true;
        result.quietMatched = quietMatched;
        return result;
    }
    // The implicit path, and nothing searched at all (every file filtered):
    // rg's message, unless --no-messages. --files just comes back
    // empty-handed.
    if (implicitDot && filesSearched == 0 && settings.mode != RgMode::Files) {
        if (!settings.noMessages) {
            context.ErrorText("rg: No files were searched, which means ripgrep probably applied "
                "a filter you didn't expect.\nRunning with --debug will show why files are "
                "being skipped.\n");
        }
        result.noFilesSearched = true;
    }
    return result;
}

} // namespace Haisos