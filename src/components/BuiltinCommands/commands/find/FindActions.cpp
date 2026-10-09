// The actions of find (this task's primaries, FindActionPrimaries()): every
// one GNU find has, with its own argument scan, its own output formats and
// its own messages. -exec and friends run programs only through
// RunProgramAndWait, the files -fprint and friends write only through
// context.IO(); -printf scans its own directives, never ParsePrintfSpec.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "BuiltinCommand.h"
#include "BuiltinDate.h"
#include "BuiltinPrompt.h"
#include "BuiltinPrintf.h"
#include "BuiltinRunProgram.h"
#include "BuiltinText.h"
#include "commands/find/FindExpression.h"
#include "interfaces/IFileIO.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos::Find {

// What the actions share across one whole run of find (FindParseState holds
// it): the one prompt -ok and -okdir ask their questions through (it keeps
// the lines read ahead, so each answer goes to its own question), the output
// files -fprint and friends open once per distinct name, and whether -delete
// was given (the parser checks its clash with -prune once the tree is built).
class FindActionState {
public:
    std::shared_ptr<BuiltinPrompt> prompt;

    // One file a -fprint, -fprint0, -fprintf or -fls writes to, shared by
    // every primary naming the same path: the descriptor is opened at parse
    // time, the bytes buffered here until the action finishes (or the buffer
    // grows past a block).
    struct OutputFile {
        std::shared_ptr<IFileDescriptor> descriptor;  // null: /dev/stdout, /dev/stderr
        bool isStderr = false;
        std::string buffer;
    };
    std::map<std::string, std::shared_ptr<OutputFile>> outputFiles;

    bool sawDelete = false;
};

namespace {

// GNU's limit on a whole batch of -exec ... +: the command, its arguments
// and every path on it, each with the byte between them, may not take more.
constexpr size_t kExecPlusLimit = 131072;

// ls's recent-time window, in seconds: within it -ls and %l show the clock
// time, beyond it the year (find's own, as ls's).
constexpr int64_t kSixMonths = 31556952 / 2;

// The output files flush once their buffer grows past one block, as a stdout
// to a file is block-buffered.
constexpr size_t kOutputFlushSize = 4096;

// A primary of its own kind, as the one pointer type every parser hands
// around (FindTests.cpp's MakePrimary, for this file's anonymous namespace).
template <typename T, typename... A>
std::unique_ptr<FindPrimary> MakePrimary(A&&... args) {
    return std::make_unique<T>(std::forward<A>(args)...);
}

// The one FindActionState of the run, made by the first action parsed.
FindActionState& ActionsOf(FindParser& parser) {
    auto& actions = parser.State().actions;
    if (!actions) {
        actions = std::make_shared<FindActionState>();
    }
    return *actions;
}

std::string RightIn(const std::string& text, size_t width) {
    return text.size() >= width ? text : std::string(width - text.size(), ' ') + text;
}

std::string LeftIn(const std::string& text, size_t width) {
    return text.size() >= width ? text : text + std::string(width - text.size(), ' ');
}

// Nanoseconds as GNU's time directives print them: nine digits, with the
// tenth digit 0 it always appends (its own buffer counts in int, not long).
std::string NanosecondDigits(uint32_t nanoseconds) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%09u0", nanoseconds);
    return buf;
}

// Which of a file's three times a directive looks at.
const FileDateTime& FieldTimeOf(const FindFile& file, char which) {
    switch (which) {
    case 'a': return file.status.accessTime;
    case 'c': return file.status.changeTime;
    default: return file.status.modificationTime;
    }
}

// %a, %c and %t: ctime's own shape -- week day, month and day, clock time,
// the nanoseconds, then the year -- through FormatDateTime's own formatting.
std::string CtimeStyleTime(const FileDateTime& time) {
    return FormatDateTime("%a %b %e %H:%M:%S", time, false)
        + "." + NanosecondDigits(time.nanoseconds)
        + " " + FormatDateTime("%Y", time, false);
}

// %Ak, %Ck, %Tk: k's own form. @, +, S, T and X are shapes of their own; a
// letter is a strftime conversion; anything else prints just "%", with k,
// as GNU's format_timespec prints it.
std::string TimeWithKind(const FileDateTime& time, char kind) {
    switch (kind) {
    case '@':
        return std::to_string(time.seconds) + "." + NanosecondDigits(time.nanoseconds);
    case '+':
        return FormatDateTime("%Y-%m-%d+%H:%M:%S", time, false)
            + "." + NanosecondDigits(time.nanoseconds);
    case 'S':
        return FormatDateTime("%S", time, false) + "." + NanosecondDigits(time.nanoseconds);
    case 'T':
    case 'X':
        return FormatDateTime("%H:%M:%S", time, false) + "." + NanosecondDigits(time.nanoseconds);
    default:
        if ((kind >= 'A' && kind <= 'Z') || (kind >= 'a' && kind <= 'z')) {
            return FormatDateTime(std::string("%") + kind, time, false);
        }
        return std::string("%") + kind;
    }
}

// The mode column -ls and %M print: HaisosOS has no permissions, so every
// file is rwxrwxrwx, with the type letter GNU's would print.
std::string ModeStringOf(const FileStatus& status) {
    const char type = status.type == DirectoryEntryType::Dir ? 'd'
        : (status.type == DirectoryEntryType::CharDevice ? 'c' : '-');
    return std::string(1, type) + "rwxrwxrwx";
}

// %y and %Y's letter (HaisosOS has no links, so the two are the same).
std::string TypeLetterOf(const FileStatus& status) {
    switch (status.type) {
    case DirectoryEntryType::Dir: return "d";
    case DirectoryEntryType::CharDevice: return "c";
    default: return "f";
    }
}

// One line of -ls and -fls, GNU's own layout: the inode, the 1K blocks
// (rounded up), the mode, the links, owner and group, the size (a device's
// own major and minor numbers), the time (the clock time inside six months,
// the year beyond) and the path, each in its fixed column.
std::string LsLineOf(const FindFile& file) {
    const FileStatus& status = file.status;
    std::string size;
    if (status.type == DirectoryEntryType::CharDevice) {
        size = std::to_string(status.deviceMajor) + ", "
            + RightIn(std::to_string(status.deviceMinor), 3);
    } else {
        size = std::to_string(status.size);
    }
    const FileDateTime& mtime = status.modificationTime;
    const int64_t now = CurrentFileDateTime().seconds;
    const bool recent = mtime.seconds <= now && now - mtime.seconds < kSixMonths;
    return RightIn("0", 9) + " "
        + RightIn(std::to_string((status.blocks + 1) / 2), 6) + " "
        + ModeStringOf(status) + " "
        + RightIn(std::to_string(status.linkCount), 3) + " "
        + LeftIn("haisos", 8) + " "
        + LeftIn("haisos", 8) + " "
        + RightIn(size, 8) + " "
        + FormatDateTime(recent ? "%b %e %H:%M" : "%b %e  %Y", mtime, false) + " "
        + file.path + "\n";
}

// Where a printing action's bytes go: find's own stdout, or one of the files
// -fprint and friends name. A file is buffered until the action finishes (or
// its buffer grows past a block); find's own stdout is context.Out's buffer,
// so -print's own order is kept; /dev/stderr is written unbuffered, exactly
// the bytes, and /dev/stdout is the same stdout as -print's.
class FindSink {
public:
    FindSink() = default;
    explicit FindSink(std::shared_ptr<FindActionState::OutputFile> file)
        : m_file(std::move(file)) {}

    void Write(FindRun& run, const std::string& bytes) {
        if (!m_file || !m_file->descriptor) {
            if (m_file && m_file->isStderr) {
                run.context.ErrorText(bytes);
            } else {
                run.context.Out(bytes);
            }
            return;
        }
        m_file->buffer += bytes;
        if (m_file->buffer.size() >= kOutputFlushSize) {
            Flush(run);
        }
    }

    void Flush(FindRun& run) {
        if (m_file && m_file->descriptor && !m_file->buffer.empty()) {
            WriteFully(*m_file->descriptor, m_file->buffer);
            m_file->buffer.clear();
        }
    }

private:
    std::shared_ptr<FindActionState::OutputFile> m_file;  // null: find's own stdout
};

// The FILE of -fprint, -fprint0, -fprintf and -fls: opened once per
// distinct name, at parse time, created and truncated. /dev/stdout and
// /dev/stderr are find's own two streams. Fail(...) when it cannot be made.
std::shared_ptr<FindActionState::OutputFile> OpenOutputFile(FindParser& parser,
                                                             const std::string& path) {
    FindActionState& actions = ActionsOf(parser);
    const auto existing = actions.outputFiles.find(path);
    if (existing != actions.outputFiles.end()) {
        return existing->second;
    }
    auto file = std::make_shared<FindActionState::OutputFile>();
    if (path == "/dev/stderr") {
        file->isStderr = true;
    } else if (path != "/dev/stdout") {
        file->descriptor = parser.Context().IO().OpenFile(path, kFileOpenWriteCreateTruncate,
                                                          kFileCreateMode);
        if (!file->descriptor) {
            // Worded as GNU's open failure is: a name that names nothing, a
            // directory, and everything else it was not allowed to open.
            std::string reason = "Permission denied";
            FileStatus status;
            if (parser.Context().IO().Stat(path, status) != 0) {
                reason = "No such file or directory";
            } else if (status.type == DirectoryEntryType::Dir) {
                reason = "Is a directory";
            }
            parser.Fail(GnuQuote(path) + ": " + reason);
            return nullptr;
        }
    }
    actions.outputFiles[path] = file;
    return file;
}

// One piece of a -printf format: literal bytes (escapes resolved as the scan
// read them), or one directive with the flags, width and precision GNU's
// find takes. Directives GNU does not know were warned about and turned
// into literals at parse time; a \c ends the format there.
struct FindFormatPiece {
    std::string literal;   // when !directive: the bytes to print
    bool directive = false;
    PrintfSpec spec;       // conversion: the directive letter, as scanned
    char timeKind = 0;     // %A/%C/%T/%B's format character
};

// Scans |format| the way GNU's pred_printf does, not ParsePrintfSpec's (the
// two know different things). Flags from "-+ #0", a width, ".precision", one
// conversion character; the escapes \a \b \f \n \r \t \v \\, \NNN (with \0)
// and \c, which drops everything after it; the time directives %A %C %T %B
// take one more character. What it does not know it says so, always (not
// only under -warn), and writes out as it stands. False after Fail(...).
bool ParseFindFormat(FindParser& parser, const std::string& format,
                     std::vector<FindFormatPiece>& pieces) {
    // The actions' warnings are always on: -warn does not turn them off.
    const auto warn = [&parser](const std::string& message) {
        parser.Context().Error("warning: " + message);
    };
    std::string literal;
    const auto flushLiteral = [&]() {
        if (!literal.empty()) {
            FindFormatPiece text;
            text.literal = literal;
            literal.clear();
            pieces.push_back(std::move(text));
        }
    };
    size_t i = 0;
    while (i < format.size()) {
        const char c = format[i];
        if (c == '\\') {
            if (i + 1 >= format.size()) {
                literal += '\\';  // a backslash alone, as GNU leaves it
                break;
            }
            const char escape = format[i + 1];
            if (escape == 'c') {
                // \c: this file's output stops here, and what follows is not
                // even scanned (no warning for it either).
                flushLiteral();
                return true;
            }
            if (escape >= '0' && escape <= '7') {
                // One to three octal digits, \0 among them.
                int value = 0;
                size_t j = i + 1;
                for (int digits = 0; digits < 3 && j < format.size()
                     && format[j] >= '0' && format[j] <= '7'; ++digits, ++j) {
                    value = value * 8 + (format[j] - '0');
                }
                literal += static_cast<char>(value);
                i = j;
                continue;
            }
            switch (escape) {
            case 'a': literal += '\a'; break;
            case 'b': literal += '\b'; break;
            case 'f': literal += '\f'; break;
            case 'n': literal += '\n'; break;
            case 'r': literal += '\r'; break;
            case 't': literal += '\t'; break;
            case 'v': literal += '\v'; break;
            case '\\': literal += '\\'; break;
            default:
                warn("unrecognized escape `\\" + std::string(1, escape) + "'");
                literal += '\\';
                literal += escape;
                break;
            }
            i += 2;
            continue;
        }
        if (c != '%') {
            literal += c;
            ++i;
            continue;
        }
        // '%': the flags, a width, a precision, one conversion character.
        if (i + 1 >= format.size()) {
            parser.Fail("error: % at end of format string");
            return false;
        }
        size_t j = i + 1;
        FindFormatPiece piece;
        piece.directive = true;
        while (j < format.size() && (format[j] == '-' || format[j] == '+'
            || format[j] == ' ' || format[j] == '#' || format[j] == '0')) {
            piece.spec.flags += format[j];
            ++j;
        }
        if (j < format.size() && format[j] >= '1' && format[j] <= '9') {
            int width = 0;
            while (j < format.size() && format[j] >= '0' && format[j] <= '9') {
                width = width * 10 + (format[j] - '0');
                ++j;
            }
            piece.spec.width = width;
        }
        if (j < format.size() && format[j] == '.') {
            int precision = 0;
            ++j;
            while (j < format.size() && format[j] >= '0' && format[j] <= '9') {
                precision = precision * 10 + (format[j] - '0');
                ++j;
            }
            piece.spec.precision = precision;  // "." alone is precision 0
        }
        if (j >= format.size()) {
            parser.Fail("error: % at end of format string");
            return false;
        }
        const char conversion = format[j];
        piece.spec.conversion = conversion;
        ++j;
        if (conversion == 'A' || conversion == 'C' || conversion == 'T' || conversion == 'B') {
            if (j >= format.size()) {
                warn("format directive `%" + std::string(1, conversion)
                    + "' should be followed by another character");
                piece.directive = false;
                piece.literal = format.substr(i, j - i);  // as it stands
            } else {
                piece.timeKind = format[j];
                ++j;
            }
        } else if (std::string("pPfhHdskbniDmMuGgUGyYlFZactS%")
                       .find(conversion) == std::string::npos) {
            warn("unrecognized format directive `%" + std::string(1, conversion) + "'");
            piece.directive = false;
            piece.literal = format.substr(i, j - i);  // printed as written
        }
        flushLiteral();
        pieces.push_back(std::move(piece));
        i = j;
    }
    flushLiteral();
    return true;
}

// One directive's value for |file| under the spec it was scanned with. Only
// %d, %m and %S are formatted as integer, unsigned and float; everything
// else is a string (FormatPrintfString, whose padding is GNU's own, spaces
// even under the 0 flag).
std::string RawDirectiveValue(const FindFile& file, const FindFormatPiece& piece) {
    const FileStatus& status = file.status;
    const char directive = piece.spec.conversion;
    switch (directive) {
    case 'p': return file.path;
    case 'P': {
        // The path without the starting point and its '/' (nothing for the
        // point itself).
        const std::string& point = file.startingPoint;
        if (file.path.compare(0, point.size(), point) == 0) {
            if (file.path.size() == point.size()) {
                return "";
            }
            if (file.path[point.size()] == '/') {
                return file.path.substr(point.size() + 1);
            }
        }
        return file.path;
    }
    case 'f': return file.name;
    case 'h': {
        // The path's leading directories: everything before the last '/',
        // "." when there is none, "" for "/" itself.
        const size_t slash = file.path.rfind('/');
        if (slash == std::string::npos) {
            return ".";
        }
        if (slash == 0) {
            return "";
        }
        return file.path.substr(0, slash);
    }
    case 'H': return file.startingPoint;
    case 'd': {
        PrintfSpec spec = piece.spec;
        spec.conversion = 'd';
        return FormatPrintfSigned(spec, file.depth);
    }
    case 's': return std::to_string(status.size);
    case 'k': return std::to_string((status.blocks + 1) / 2);
    case 'b': return std::to_string(status.blocks);
    case 'n': return std::to_string(status.linkCount);
    case 'i': return "0";   // HaisosOS reports no inode numbers
    case 'D': return "0";
    case 'm': {
        PrintfSpec spec = piece.spec;
        spec.conversion = 'o';
        return FormatPrintfUnsigned(spec, 0777);
    }
    case 'M': return ModeStringOf(status);
    case 'u': return "haisos";
    case 'g': return "haisos";
    case 'U': return "0";
    case 'G': return "0";
    case 'y': return TypeLetterOf(status);
    case 'Y': return TypeLetterOf(status);  // no links: %Y is %y
    case 'l': return "";                   // no links: no object of one
    case 'F': return "unknown";
    case 'Z': return "";                   // no SELinux: no context
    case 'S': {
        PrintfSpec spec = piece.spec;
        spec.conversion = 'g';
        const double ratio = status.size == 0 ? 1.0
            : static_cast<double>(status.blocks * 512) / static_cast<double>(status.size);
        return FormatPrintfFloat(spec, ratio);
    }
    case 'a': return CtimeStyleTime(status.accessTime);
    case 'c': return CtimeStyleTime(status.changeTime);
    case 't': return CtimeStyleTime(status.modificationTime);
    case 'A': case 'C': case 'T':
        return TimeWithKind(FieldTimeOf(file, directive == 'A' ? 'a' : (directive == 'C' ? 'c' : 't')),
                            piece.timeKind);
    case 'B': return "";  // HaisosOS cannot tell a file's birth time
    case '%': return FormatPrintfString(piece.spec, "%");
    }
    return "";
}

// The value padded and truncated by the spec it was scanned with: %d, %m, %S
// and %% applied theirs already; every other directive is a plain string.
std::string DirectiveValue(const FindFile& file, const FindFormatPiece& piece) {
    switch (piece.spec.conversion) {
    case 'd': case 'm': case 'S': case '%':
        return RawDirectiveValue(file, piece);
    }
    return FormatPrintfString(piece.spec, RawDirectiveValue(file, piece));
}

// -printf FMT and -fprintf FILE FMT: the format's bytes, directive by
// directive. Always true.
class FindPrintfPrimary : public FindPrimary {
public:
    FindPrintfPrimary(std::vector<FindFormatPiece> pieces, FindSink sink)
        : m_pieces(std::move(pieces)), m_sink(std::move(sink)) {}

    bool Evaluate(FindRun& run, const FindFile& file) override {
        std::string out;
        for (const FindFormatPiece& piece : m_pieces) {
            if (piece.directive) {
                out += DirectiveValue(file, piece);
            } else {
                out += piece.literal;
            }
        }
        m_sink.Write(run, out);
        return true;
    }

    void Finish(FindRun& run) override {
        m_sink.Flush(run);
    }

private:
    std::vector<FindFormatPiece> m_pieces;
    FindSink m_sink;
};

// -fprint FILE and -fprint0 FILE: the path, with the delimiter of their own.
class FindFprintPrimary : public FindPrimary {
public:
    FindFprintPrimary(char delimiter, FindSink sink)
        : m_delimiter(delimiter), m_sink(std::move(sink)) {}

    bool Evaluate(FindRun& run, const FindFile& file) override {
        m_sink.Write(run, file.path + m_delimiter);
        return true;
    }

    void Finish(FindRun& run) override {
        m_sink.Flush(run);
    }

private:
    char m_delimiter;
    FindSink m_sink;
};

// -ls and -fls FILE: one line per file, GNU -ls's own layout.
class FindLsPrimary : public FindPrimary {
public:
    explicit FindLsPrimary(FindSink sink) : m_sink(std::move(sink)) {}

    bool Evaluate(FindRun& run, const FindFile& file) override {
        m_sink.Write(run, LsLineOf(file));
        return true;
    }

    void Finish(FindRun& run) override {
        m_sink.Flush(run);
    }

private:
    FindSink m_sink;
};

// The parsing of -printf and -fprintf, which differ only in their FILE.
std::unique_ptr<FindPrimary> ParsePrintfPrimary(FindParser& parser, const std::string& name,
                                                bool toFile) {
    FindSink sink;
    if (toFile) {
        std::string path;
        if (!parser.NextArgument(name, path)) {
            return nullptr;
        }
        auto file = OpenOutputFile(parser, path);
        if (!file) {
            return nullptr;
        }
        sink = FindSink(file);
    }
    std::string format;
    if (!parser.NextArgument(name, format)) {
        return nullptr;
    }
    std::vector<FindFormatPiece> pieces;
    if (!ParseFindFormat(parser, format, pieces)) {
        return nullptr;
    }
    return MakePrimary<FindPrintfPrimary>(std::move(pieces), std::move(sink));
}

// -delete: the walk's depth-first order is what deleting a tree needs, so
// the primary turns -depth on at parse time. GNU's own wording when it
// cannot, and the starting point "." itself is left alone.
class FindDeletePrimary : public FindPrimary {
public:
    bool Evaluate(FindRun& run, const FindFile& file) override {
        if (file.path == ".") {
            return true;
        }
        const bool isDir = file.status.type == DirectoryEntryType::Dir;
        const int result = isDir ? run.context.IO().RemoveDirectory(file.path)
                                 : run.context.IO().RemoveFile(file.path);
        if (result == 0) {
            return true;
        }
        // GNU's errno wording, with the two HaisosOS can hit: a directory
        // still holding entries, and everything else.
        std::string reason = "Permission denied";
        if (isDir) {
            for (const DirectoryEntry& entry : run.context.IO().ReadDirectory(file.path)) {
                if (entry.name != "." && entry.name != "..") {
                    reason = "Directory not empty";
                    break;
                }
            }
        }
        run.context.Error("cannot delete " + GnuQuote(file.path) + ": " + reason);
        run.exitStatus = 1;
        return false;
    }
};

// -exec/-execdir/-ok/-okdir's argument list, scanned as GNU's collect_args
// does. ";" ends it; "+" ends -exec's and -execdir's (never -ok's and
// -okdir's) when the word before it is "{}" alone -- then the paths are
// appended to the command, in batches; anything else is one more argument,
// so a "+" where it does not belong ends in "missing argument".
struct ExecCommand {
    std::vector<std::string> words;  // the command and its arguments, as given
    bool plus = false;               // ... {} +: the paths go onto the command
};

std::unique_ptr<ExecCommand> ParseExecArguments(FindParser& parser, const std::string& name,
                                                bool prompt) {
    auto command = std::make_unique<ExecCommand>();
    std::string arg;
    for (;;) {
        if (!parser.NextArgument(name, arg)) {
            return nullptr;
        }
        if (arg == ";") {
            if (command->words.empty()) {
                parser.Fail("invalid argument `;' to `" + name + "'");
                return nullptr;
            }
            return command;
        }
        if (!prompt && arg == "+" && !command->words.empty()
            && command->words.back().find("{}") != std::string::npos) {
            int braces = 0;
            for (const std::string& word : command->words) {
                if (word.find("{}") != std::string::npos) {
                    ++braces;
                }
            }
            if (braces > 1) {
                parser.Fail("Only one instance of {} is supported with " + name + " ... +");
                return nullptr;
            }
            if (command->words.back() != "{}") {
                parser.Fail("In '" + name + " ... {} +' the '{}' must appear by itself, but you specified "
                    + GnuQuote(command->words.back()));
                return nullptr;
            }
            command->words.pop_back();  // the paths take its place
            command->plus = true;
            return command;
        }
        command->words.push_back(arg);
    }
}

// -execdir and -okdir run a program from the file's own directory, so an
// empty or relative PATH entry would let a file there shadow the real
// command: GNU refuses, at the first of them, naming the entry. False after
// Fail(...).
bool CheckExecdirPath(FindParser& parser, const std::string& name) {
    for (const std::string& entry : SearchPathEntries(parser.Context())) {
        if (entry.empty() || entry == ".") {
            parser.Fail("The current directory is included in the PATH environment variable, which is insecure in combination with the "
                + name + " action of find.  Please remove the current directory from your $PATH (that is, remove \".\", doubled colons, or leading or trailing colons)");
            return false;
        }
        if (entry[0] != '/') {
            parser.Fail("The relative path " + GnuQuote(entry)
                + " is included in the PATH environment variable, which is insecure in combination with the "
                + name + " action of find.  Please remove that entry from $PATH");
            return false;
        }
    }
    return true;
}

// -exec CMD, -exec CMD {} +, -execdir, -ok and -okdir: a program run on the
// file, waited for. ";" runs it once per file, true when it exits 0; "+"
// batches the paths onto one command, always true, and the last batch goes
// in Finish. -execdir and -okdir run it in the file's directory, with {} the
// "./" name ("./." for the starting point "."); -ok and -okdir ask first,
// on one prompt the whole run shares.
class FindExecPrimary : public FindPrimary {
public:
    FindExecPrimary(std::vector<std::string> words, bool plus, bool dir, bool prompt,
                    std::shared_ptr<FindActionState> state)
        : m_words(std::move(words)), m_plus(plus), m_dir(dir), m_prompt(prompt),
          m_state(std::move(state)) {}

    bool Evaluate(FindRun& run, const FindFile& file) override {
        // -execdir's directory, and the "./" name {} stands for.
        std::string workingDirectory;
        std::string substitute = file.path;
        if (m_dir) {
            const size_t slash = file.path.rfind('/');
            const std::string base = slash == std::string::npos ? file.path
                : file.path.substr(slash + 1);
            workingDirectory = slash == std::string::npos ? "."
                : (slash == 0 ? "/" : file.path.substr(0, slash));
            substitute = file.path == "/" ? "/" : "./" + base;
        }
        if (m_prompt) {
            const std::string question = "< " + m_words[0] + " ... " + file.path + " > ? ";
            if (!m_state->prompt || !m_state->prompt->Ask(question)) {
                return false;
            }
        }
        if (m_plus) {
            // One batch while the directory holds and the size holds (GNU
            // counts the command, its arguments and the paths, each with
            // its separating byte); the rest goes in Finish.
            const bool directoryChanged = m_dir && !m_paths.empty()
                && workingDirectory != m_lastDirectory;
            const bool tooBig = BatchSize() + substitute.size() + 1 > kExecPlusLimit;
            if (!m_paths.empty() && (directoryChanged || tooBig)) {
                RunBatch(run);
            }
            m_lastDirectory = workingDirectory;
            m_paths.push_back(substitute);
            return true;
        }
        std::vector<std::string> arguments;
        arguments.reserve(m_words.size());
        for (const std::string& word : m_words) {
            arguments.push_back(SubstituteBraces(word, substitute));
        }
        RunProgramOptions options;
        if (m_dir) {
            options.workingDirectory = workingDirectory;
        }
        if (m_prompt) {
            options.stdIn = OpenEmptyInput(run.context);
        }
        return RunSemicolon(run, arguments, options);
    }

    void Finish(FindRun& run) override {
        if (m_plus && !m_paths.empty()) {
            RunBatch(run);
        }
    }

private:
    // Every {} in |word| -- the command slot included, wherever it stands
    // (x{}y too) -- replaced with |path|.
    static std::string SubstituteBraces(const std::string& word, const std::string& path) {
        std::string out;
        size_t pos = 0;
        for (;;) {
            const size_t brace = word.find("{}", pos);
            if (brace == std::string::npos) {
                out += word.substr(pos);
                return out;
            }
            out += word.substr(pos, brace - pos);
            out += path;
            pos = brace + 2;
        }
    }

    // The bytes the batch takes so far, GNU's own count.
    size_t BatchSize() const {
        size_t size = 0;
        for (const std::string& word : m_words) {
            size += word.size() + 1;
        }
        for (const std::string& path : m_paths) {
            size += path.size() + 1;
        }
        return size;
    }

    // Where the program runs from. A command that stays the same is looked
    // up once, at its first use; one holding {} changes with the file and is
    // looked up again. -execdir's command with a '/' that is not absolute
    // (./{} among them) is taken from the file's directory, where it runs.
    bool ResolveProgram(FindRun& run, const std::string& command, const std::string& directory) {
        std::string lookup = command;
        if (m_dir && !command.empty() && command[0] != '/'
            && command.find('/') != std::string::npos) {
            lookup = (directory == "/" ? std::string("/") : directory + "/") + command;
        }
        if (!m_resolved || lookup != m_resolvedName) {
            m_program = FindProgramInPath(run.context, lookup);
            m_resolvedName = lookup;
            m_resolved = true;
        }
        return m_program.has_value();
    }

    // One ";"-form run: true when the command exits 0. A program that is not
    // there is reported each time, as GNU does, and the primary is false;
    // the exit status is left as it was.
    bool RunSemicolon(FindRun& run, const std::vector<std::string>& arguments,
                     const RunProgramOptions& options) {
        const std::string& command = arguments.front();
        if (!ResolveProgram(run, command, options.workingDirectory.value_or(std::string()))) {
            ReportMissing(run, command);
            return false;
        }
        std::vector<std::string> args(arguments.begin() + 1, arguments.end());
        bool started = false;
        const int code = RunProgramAndWait(run.context, *m_program, args, options, &started);
        if (!started) {
            // Found, but it would not start: GNU's execve failure wording.
            run.context.Error(GnuQuote(command) + ": Permission denied");
            return false;
        }
        return code == 0;
    }

    // One "+"-form batch: the paths collected so far, appended to the
    // command (with no command of its own, the first path is it, as -exec {}
    // +). Always true; a command that fails, or one that cannot be run at
    // all, makes find exit 1.
    void RunBatch(FindRun& run) {
        const std::string& command = m_words.empty() ? m_paths.front() : m_words.front();
        std::vector<std::string> arguments;
        if (m_words.empty()) {
            arguments.assign(m_paths.begin() + 1, m_paths.end());
        } else {
            arguments.reserve(m_words.size() - 1 + m_paths.size());
            arguments.insert(arguments.end(), m_words.begin() + 1, m_words.end());
            arguments.insert(arguments.end(), m_paths.begin(), m_paths.end());
        }
        RunProgramOptions options;
        if (m_dir) {
            options.workingDirectory = m_lastDirectory;
        }
        if (!ResolveProgram(run, command, m_lastDirectory)) {
            ReportMissing(run, command);
            run.exitStatus = 1;
        } else {
            bool started = false;
            const int code = RunProgramAndWait(run.context, *m_program, arguments, options, &started);
            if (!started) {
                run.context.Error(GnuQuote(command) + ": Permission denied");
            }
            if (!started || code != 0) {
                run.exitStatus = 1;
            }
        }
        m_paths.clear();
    }

    static void ReportMissing(FindRun& run, const std::string& command) {
        run.context.Error(GnuQuote(command) + ": No such file or directory");
    }

    std::vector<std::string> m_words;
    bool m_plus;
    bool m_dir;
    bool m_prompt;
    std::shared_ptr<FindActionState> m_state;
    std::vector<std::string> m_paths;  // "+"-form: the batch collected so far
    std::string m_lastDirectory;       // -execdir ... +: its batch's directory
    std::optional<std::string> m_program;
    std::string m_resolvedName;        // the name m_program was looked up as
    bool m_resolved = false;
};

// The parsing of the four -exec primaries, which differ in their directory
// and their prompting (and in the PATH they demand).
std::unique_ptr<FindPrimary> ParseExecPrimary(FindParser& parser, const std::string& name,
                                              bool dir, bool prompt) {
    if (dir && !CheckExecdirPath(parser, name)) {
        return nullptr;
    }
    FindActionState& actions = ActionsOf(parser);
    if (prompt && !actions.prompt) {
        actions.prompt = std::make_shared<BuiltinPrompt>(parser.Context());
    }
    auto command = ParseExecArguments(parser, name, prompt);
    if (!command) {
        return nullptr;
    }
    return MakePrimary<FindExecPrimary>(std::move(command->words), command->plus, dir, prompt,
                                        parser.State().actions);
}

// The FILE argument and the record behind it, for the four -f... primaries.
std::unique_ptr<FindPrimary> ParseFileAction(FindParser& parser, const std::string& name,
                                             const std::function<std::unique_ptr<FindPrimary>(
                                                 FindSink)>& make) {
    std::string path;
    if (!parser.NextArgument(name, path)) {
        return nullptr;
    }
    auto file = OpenOutputFile(parser, path);
    if (!file) {
        return nullptr;
    }
    return make(FindSink(file));
}

}  // namespace

bool FindDeleteWasGiven(const FindParseState& state) {
    return state.actions && state.actions->sawDelete;
}

const std::vector<FindPrimaryEntry>& FindActionPrimaries() {
    static const std::vector<FindPrimaryEntry> rows = {
        {"-delete", FindPrimaryKind::Action, true,
            [](FindParser& parser, const std::string&) {
                // Deleting needs the children first: -depth, implicit.
                parser.Settings().depthFirst = true;
                ActionsOf(parser).sawDelete = true;
                return MakePrimary<FindDeletePrimary>();
            }},
        {"-exec", FindPrimaryKind::Action, true,
            [](FindParser& parser, const std::string& name) {
                return ParseExecPrimary(parser, name, false, false);
            }},
        {"-execdir", FindPrimaryKind::Action, true,
            [](FindParser& parser, const std::string& name) {
                return ParseExecPrimary(parser, name, true, false);
            }},
        {"-fls", FindPrimaryKind::Action, true,
            [](FindParser& parser, const std::string& name) {
                return ParseFileAction(parser, name, [](FindSink sink) {
                    return MakePrimary<FindLsPrimary>(std::move(sink));
                });
            }},
        {"-fprint", FindPrimaryKind::Action, true,
            [](FindParser& parser, const std::string& name) {
                return ParseFileAction(parser, name, [](FindSink sink) {
                    return MakePrimary<FindFprintPrimary>('\n', std::move(sink));
                });
            }},
        {"-fprint0", FindPrimaryKind::Action, true,
            [](FindParser& parser, const std::string& name) {
                return ParseFileAction(parser, name, [](FindSink sink) {
                    return MakePrimary<FindFprintPrimary>('\0', std::move(sink));
                });
            }},
        {"-fprintf", FindPrimaryKind::Action, true,
            [](FindParser& parser, const std::string& name) {
                return ParsePrintfPrimary(parser, name, true);
            }},
        {"-ls", FindPrimaryKind::Action, true,
            [](FindParser&, const std::string&) {
                return MakePrimary<FindLsPrimary>(FindSink());
            }},
        {"-ok", FindPrimaryKind::Action, true,
            [](FindParser& parser, const std::string& name) {
                return ParseExecPrimary(parser, name, false, true);
            }},
        {"-okdir", FindPrimaryKind::Action, true,
            [](FindParser& parser, const std::string& name) {
                return ParseExecPrimary(parser, name, true, true);
            }},
        {"-printf", FindPrimaryKind::Action, true,
            [](FindParser& parser, const std::string& name) {
                return ParsePrintfPrimary(parser, name, false);
            }},
    };
    return rows;
}

}  // namespace Haisos::Find