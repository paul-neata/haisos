#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinSize.h"
#include "BuiltinText.h"
#include "interfaces/IHaisosOS.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {
namespace {

enum TailOption {
    kTailBytes = 1,
    kTailFollow,
    kTailFollowNameRetry,  // -F
    kTailLines,
    kTailPid,
    kTailQuiet,
    kTailRetry,
    kTailSleep,
    kTailVerbose,
    kTailZero,
    kTailDigit,  // +NUM/-NUM given outside the obsolete form
};

enum class TailHeader { Multiple, Never, Always };

enum class TailFollow { None, Descriptor, Name };

// The count option in effect: the last n lines/bytes of each file, or with
// fromStart the whole file from its n'th line/byte on (n counted from one,
// as GNU's +NUM).
struct TailCount {
    bool bytes = false;
    bool fromStart = false;
    uintmax_t n = 10;
};

// One FILE being followed: the descriptor (null while the file is gone and
// --retry is watching for it), and how far it has been read -- where the next
// round picks the file up, and what a Stat compares its size against.
struct TailFile {
    std::string name;
    std::shared_ptr<IFileDescriptor> descriptor;
    uintmax_t position = 0;
    bool reportedInaccessible = false;
};

// The size value a -n/-c option takes: a leading '+' means "from the NUM'th
// on" and a leading '-' is dropped (the default). Reports and returns false
// on a bad value.
bool ParseTailCount(BuiltinContext& context, const std::string& text, bool bytes, TailCount& out) {
    std::string value = text;
    out.fromStart = !value.empty() && value[0] == '+';
    if (!value.empty() && (value[0] == '+' || value[0] == '-')) {
        value.erase(0, 1);
    }
    out.bytes = bytes;
    const SizeParse result = ParseSizeWithSuffix(value, "bkKmMGTPEZYRQ0", out.n);
    if (result == SizeParse::Ok) {
        return true;
    }
    const std::string kind = bytes ? "bytes" : "lines";
    if (result == SizeParse::Overflow) {
        context.Error("invalid number of " + kind + ": " + GnuQuote(value)
            + ": Value too large for defined data type");
    } else {
        context.Error("invalid number of " + kind + ": " + GnuQuote(value));
    }
    return false;
}

// Whether an argument is an option: "-", "--" and a lone "-" are operands
// (GNU's obsolete-form check asks this about the second argument).
bool IsOptionWord(const std::string& arg) {
    return arg.size() > 1 && arg[0] == '-' && arg != "--";
}

enum class Obsolete { No, Yes, Error };

// GNU's parse_obsolete_option: the first argument "+NUM" or "-NUM", with an
// optional b (512 bytes), c (bytes) or l (lines) unit and an optional trailing
// f (--follow, by descriptor), when the argument list is short enough that
// the first argument cannot be an option of the regular syntax -- decided by
// the caller. The number defaults to 10.
Obsolete ParseTailObsolete(BuiltinContext& context, const std::string& arg,
                           TailCount& count, bool& follow) {
    if (arg.size() < 2 || (arg[0] != '+' && arg[0] != '-')) {
        return Obsolete::No;
    }
    uintmax_t n = 0;
    bool overflow = false;
    bool digits = false;
    size_t i = 1;
    for (; i < arg.size() && arg[i] >= '0' && arg[i] <= '9'; ++i) {
        digits = true;
        const uintmax_t digit = static_cast<uintmax_t>(arg[i] - '0');
        if (n > (UINTMAX_MAX - digit) / 10) {
            overflow = true;
        } else {
            n = n * 10 + digit;
        }
    }
    char unit = 'l';
    if (i < arg.size() && (arg[i] == 'b' || arg[i] == 'c' || arg[i] == 'l')) {
        unit = arg[i];
        ++i;
    }
    if (i < arg.size() && arg[i] == 'f') {
        follow = true;
        ++i;
    }
    if (i != arg.size()) {
        return Obsolete::No;  // not the obsolete form after all
    }
    // "-c" is the regular option; "+c" and "-cf" are the obsolete form.
    if (arg[0] == '-' && !digits && unit == 'c' && !follow) {
        return Obsolete::No;
    }
    if (unit == 'b') {
        if (n > UINTMAX_MAX / 512) {
            overflow = true;
        } else {
            n *= 512;
        }
    }
    if (overflow) {
        context.Error("invalid number: " + GnuQuote(arg) + ": Numerical result out of range");
        return Obsolete::Error;
    }
    count.bytes = unit != 'l';
    count.fromStart = arg[0] == '+';
    count.n = n;
    return Obsolete::Yes;
}

// -s: any non-negative number whose text strtod consumes whole.
bool ParseTailSeconds(BuiltinContext& context, const std::string& text, double& out) {
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (text.empty() || end != text.c_str() + text.size()
        || value < 0 || !std::isfinite(value)) {
        context.Error("invalid number of seconds: " + GnuQuote(text));
        return false;
    }
    out = value;
    return true;
}

// --pid: a decimal pid, as strtoul would take it.
bool ParseTailPid(BuiltinContext& context, const std::string& text, uint64_t& out) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) {
        context.Error("invalid PID: " + GnuQuote(text));
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
    if (errno == ERANGE || *end != '\0') {
        context.Error("invalid PID: " + GnuQuote(text));
        return false;
    }
    out = value;
    return true;
}

// A read error on an input GNU opened: reported, and the command goes on
// (or, when following, the file is read again next round).
void TailReadFailed(BuiltinContext& context, const std::string& shownName) {
    context.Error("error reading " + ShellEscapeQuoted(shownName, /*always=*/true)
        + ": Input/output error");
}

// One input's tail, its initial read: the last n lines/bytes, or from its
// n'th on. |position| ends at the bytes consumed -- where following, if any,
// picks the file up; |printed| whether anything was written. Returns 1 on
// success, 0 on a reported read error, -1 when the process was asked to stop.
int TailOneInput(BuiltinContext& context, IFileDescriptor& input, const std::string& shownName,
                 const TailCount& count, char delimiter, uintmax_t& position, bool& printed) {
    position = 0;
    printed = false;
    char buffer[64 * 1024];
    if (count.bytes) {
        if (count.fromStart) {
            uintmax_t skip = count.n > 0 ? count.n - 1 : 0;
            while (true) {
                if (context.StopRequested()) {
                    return -1;
                }
                const ssize_t n = input.Read(buffer, sizeof(buffer));
                if (n == 0) {
                    return 1;
                }
                if (n == kIOInterrupted) {
                    return -1;
                }
                if (n < 0) {
                    TailReadFailed(context, shownName);
                    return 0;
                }
                position += static_cast<uintmax_t>(n);
                if (skip >= static_cast<uintmax_t>(n)) {
                    skip -= static_cast<uintmax_t>(n);
                    continue;
                }
                context.Out(std::string(buffer + skip, static_cast<size_t>(n) - skip));
                printed = true;
                skip = 0;
            }
        }
        // The last n bytes: kept in a ring, never more than n bytes (and one
        // read) in memory; printed at end of input.
        std::string ring;
        while (true) {
            if (context.StopRequested()) {
                return -1;
            }
            const ssize_t n = input.Read(buffer, sizeof(buffer));
            if (n == 0) {
                break;
            }
            if (n == kIOInterrupted) {
                return -1;
            }
            if (n < 0) {
                TailReadFailed(context, shownName);
                return 0;
            }
            position += static_cast<uintmax_t>(n);
            ring.append(buffer, static_cast<size_t>(n));
            if (ring.size() > count.n) {
                ring.erase(0, ring.size() - static_cast<size_t>(count.n));
            }
        }
        if (count.n > 0 && !ring.empty()) {
            context.Out(ring);
            printed = true;
        }
        return 1;
    }

    BuiltinLineReader reader(context, input, delimiter);
    std::string line;
    bool delimited = false;
    if (count.fromStart) {
        uintmax_t skip = count.n > 0 ? count.n - 1 : 0;
        while (true) {
            const LineReadResult result = reader.Next(line, delimited);
            switch (result) {
                case LineReadResult::Line: break;
                case LineReadResult::End: return 1;
                case LineReadResult::Stopped: return -1;
                case LineReadResult::Error: TailReadFailed(context, shownName); return 0;
            }
            std::string entry = line;
            if (delimited) {
                entry += delimiter;
            }
            position += entry.size();
            if (skip > 0) {
                --skip;
                continue;
            }
            context.Out(entry);
            printed = true;
        }
    }
    // The last n lines: a ring of n entries, printed at end of input.
    std::deque<std::string> ring;
    while (true) {
        const LineReadResult result = reader.Next(line, delimited);
        switch (result) {
            case LineReadResult::Line: break;
            case LineReadResult::End:
                if (count.n > 0) {
                    for (const auto& entry : ring) {
                        context.Out(entry);
                        printed = true;
                    }
                }
                return 1;
            case LineReadResult::Stopped: return -1;
            case LineReadResult::Error: TailReadFailed(context, shownName); return 0;
        }
        std::string entry = line;
        if (delimited) {
            entry += delimiter;
        }
        position += entry.size();
        ring.push_back(std::move(entry));
        if (ring.size() > count.n) {
            ring.pop_front();
        }
    }
}

// Reads and prints what was appended since the last read -- the whole file
// when it was just (re)opened -- and leaves |position| at what is consumed.
// Returns false when the process was asked to stop.
bool DumpNewData(BuiltinContext& context, TailFile& file, bool headers,
                 std::string& lastOutputName) {
    if (!file.descriptor) {
        return true;
    }
    char buffer[64 * 1024];
    std::string out;
    while (true) {
        if (context.StopRequested()) {
            return false;
        }
        const ssize_t n = file.descriptor->Read(buffer, sizeof(buffer));
        if (n == 0) {
            break;
        }
        if (n == kIOInterrupted) {
            return false;
        }
        if (n < 0) {
            TailReadFailed(context, file.name);
            break;
        }
        file.position += static_cast<uintmax_t>(n);
        out.append(buffer, static_cast<size_t>(n));
    }
    if (out.empty()) {
        return true;
    }
    // Only a file that writes something gets the header, as the multi-file
    // switch of GNU's initial output does ("\n==> name <==").
    if (headers && lastOutputName != file.name) {
        context.Out("\n==> " + file.name + " <==\n");
    }
    context.Out(out);
    context.Flush();
    lastOutputName = file.name;
    return true;
}

// Whether --pid's process is still running: a finished process leaves the
// OS's list lazily, so a listed pid is asked directly.
bool PidAlive(BuiltinContext& context, uint64_t pid) {
    auto os = context.Process().OS();
    if (!os) {
        return false;
    }
    for (const auto& process : os->GetRunningProcesses()) {
        if (process && process->GetPid() == pid) {
            return !process->WaitToFinish(0);
        }
    }
    return false;
}

class TailCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "tail"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'c', "bytes", kTailBytes, BuiltinArgument::Required, "[+]NUM",
                "print the last NUM bytes of each file; with the leading '+', print starting with the NUM'th byte"},
            {'f', "", kTailFollow, BuiltinArgument::None, "",
                "output appended data as the file grows"},
            {0, "follow", kTailFollow, BuiltinArgument::Optional, "HOW",
                "like -f, but the file's name is kept too when it moves or disappears: descriptor, name"},
            {'F', "", kTailFollowNameRetry, BuiltinArgument::None, "",
                "same as --follow=name --retry"},
            {'n', "lines", kTailLines, BuiltinArgument::Required, "[+]NUM",
                "print the last NUM lines instead of the last 10; with the leading '+', print starting with the NUM'th line"},
            {0, "max-unchanged-stats", kBuiltinNotTreated, BuiltinArgument::Required, "N"},
            {0, "pid", kTailPid, BuiltinArgument::Required, "PID",
                "with -f, terminate after process ID PID dies"},
            {0, "-presume-input-pipe", kBuiltinNotTreated},
            {0, "-disable-inotify", kBuiltinNotTreated},
            {'q', "quiet", kTailQuiet, BuiltinArgument::None, "",
                "never print headers giving file names"},
            {0, "silent", kTailQuiet, BuiltinArgument::None, "",
                "never print headers giving file names"},
            {0, "retry", kTailRetry, BuiltinArgument::None, "",
                "keep trying to open a file that is inaccessible"},
            {'s', "sleep-interval", kTailSleep, BuiltinArgument::Required, "N",
                "with -f, check the file every N seconds"},
            {'v', "verbose", kTailVerbose, BuiltinArgument::None, "",
                "always print headers giving file names"},
            {'z', "zero-terminated", kTailZero, BuiltinArgument::None, "",
                "line delimiter is NUL, not newline"},
            {'0', "", kTailDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'1', "", kTailDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'2', "", kTailDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'3', "", kTailDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'4', "", kTailDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'5', "", kTailDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'6', "", kTailDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'7', "", kTailDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'8', "", kTailDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'9', "", kTailDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "output the last part of files",
            {"tail [OPTION]... [FILE]..."},
            "NUM may have a multiplier suffix: b 512, kB 1000, K 1024, MB 1000*1000, M 1024*1024,\n"
            "and so on for T, P, E, Z, Y, R, Q; the same with a trailing B (1000-based) or iB\n"
            "(1024-based).\n"
            "With more than one FILE, precede each with a header giving the file name.\n"
            "Following is by polling: every --sleep-interval seconds (default 1.0) each file is\n"
            "asked its size and read for what is new. In --follow=name mode a file replaced by\n"
            "another of the same or larger size is not noticed until its size shrinks, and a\n"
            "file that is gone comes back only if --retry (or -F) is given. The standard\n"
            "input is never followed.",
        };
    }

    int Run(BuiltinContext& context) override {
        TailCount count;
        TailHeader header = TailHeader::Multiple;
        TailFollow followMode = TailFollow::None;
        bool retry = false;
        bool zero = false;
        bool pidGiven = false;
        uint64_t pid = 0;
        double sleepInterval = 1.0;

        std::vector<std::string> args = context.Args();
        // The obsolete form, when the argument list is short enough that the
        // first argument could not be an option of the regular syntax.
        if (!args.empty()
            && (args.size() == 1
                || (args.size() == 2 && !IsOptionWord(args[1]))
                || (args.size() == 3 && args[1] == "--"))) {
            bool follow = false;
            const Obsolete result = ParseTailObsolete(context, args[0], count, follow);
            if (result == Obsolete::Error) {
                return 1;
            }
            if (result == Obsolete::Yes) {
                followMode = TailFollow::Descriptor;
                args.erase(args.begin());
            }
        }
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, args, *this, 1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }

        for (const auto& option : parsed->options) {
            switch (option.id) {
                case kTailBytes:
                    if (!ParseTailCount(context, option.argument, true, count)) {
                        return 1;
                    }
                    break;
                case kTailLines:
                    if (!ParseTailCount(context, option.argument, false, count)) {
                        return 1;
                    }
                    break;
                case kTailFollow:
                    if (option.hasArgument) {
                        const auto matched = ArgMatch(context, "--follow", option.argument,
                            {{"descriptor", 0}, {"name", 1}});
                        if (!matched) {
                            return 1;
                        }
                        followMode = *matched == 0 ? TailFollow::Descriptor : TailFollow::Name;
                    } else {
                        followMode = TailFollow::Descriptor;
                    }
                    break;
                case kTailFollowNameRetry:
                    followMode = TailFollow::Name;
                    retry = true;
                    break;
                case kTailPid:
                    if (!ParseTailPid(context, option.argument, pid)) {
                        return 1;
                    }
                    pidGiven = true;
                    break;
                case kTailQuiet:
                    header = TailHeader::Never;
                    break;
                case kTailRetry:
                    retry = true;
                    break;
                case kTailSleep:
                    if (!ParseTailSeconds(context, option.argument, sleepInterval)) {
                        return 1;
                    }
                    break;
                case kTailVerbose:
                    header = TailHeader::Always;
                    break;
                case kTailZero:
                    zero = true;
                    break;
                case kTailDigit:
                    context.Error("option used in invalid context -- "
                        + std::string(1, option.spelling.size() > 1 ? option.spelling[1] : '0'));
                    return 1;
                default:
                    break;
            }
        }

        const bool following = followMode != TailFollow::None;
        if (!following) {
            if (retry) {
                context.Error("warning: --retry ignored; --retry is useful only when following");
            }
            if (pidGiven) {
                context.Error("warning: PID ignored; --pid=PID is useful only when following");
            }
        } else if (retry && followMode == TailFollow::Descriptor) {
            context.Error("warning: --retry only effective for the initial open");
        }

        std::vector<std::string> operands = parsed->operands;
        if (operands.empty()) {
            operands.push_back("-");
        }
        const bool headers = header == TailHeader::Always
            || (header == TailHeader::Multiple && operands.size() > 1);
        const char delimiter = zero ? '\0' : '\n';

        // The initial pass: each FILE's tail, then following starts from
        // where each read ended.
        int status = 0;
        bool printedHeader = false;
        bool printed = false;
        std::string lastOutputName;
        std::vector<TailFile> followFiles;
        std::vector<std::string> givenUpNames;  // directories: never followable
        bool anyFollowable = false;
        for (const auto& file : operands) {
            if (context.StopRequested()) {
                return 1;
            }
            const std::string shown = file == "-" ? "standard input" : file;
            InputOpenFailure failure = InputOpenFailure::None;
            auto input = OpenInputOperand(context, file, failure);
            if (failure == InputOpenFailure::Directory) {
                if (headers) {
                    PrintHeader(context, shown, printedHeader);
                }
                context.Error("error reading " + ShellEscapeQuoted(file, /*always=*/true)
                    + ": Is a directory");
                status = 1;
                if (following && file != "-") {
                    anyFollowable = true;
                    givenUpNames.push_back(file);
                }
                continue;
            }
            if (!input) {
                context.Error("cannot open " + ShellEscapeQuoted(shown, /*always=*/true)
                    + " for reading: "
                    + (failure == InputOpenFailure::Denied ? "Permission denied"
                        : failure == InputOpenFailure::BadDescriptor ? "Bad file descriptor"
                            : "No such file or directory"));
                status = 1;
                if (following && file != "-") {
                    anyFollowable = true;
                    if (retry) {
                        followFiles.push_back(TailFile{file, nullptr, 0, false});
                    }
                }
                continue;
            }
            if (headers) {
                PrintHeader(context, shown, printedHeader);
            }
            TailFile tailFile{file, input, 0, false};
            const int result = TailOneInput(context, *input, shown, count, delimiter,
                                            tailFile.position, printed);
            if (result < 0) {
                // Stopped, as a signal would stop the real command: quietly.
                return 1;
            }
            if (result == 0) {
                status = 1;
            }
            if (printed) {
                lastOutputName = file;
            }
            if (following && file != "-") {
                // The standard input is never followed (see Help).
                anyFollowable = true;
                followFiles.push_back(std::move(tailFile));
            }
        }
        if (!following) {
            return status;
        }
        for (const auto& name : givenUpNames) {
            context.Error(ShellEscapeQuoted(name) + ": cannot follow end of this type of file; "
                "giving up on this name");
        }
        if (followFiles.empty()) {
            if (anyFollowable) {
                context.Error("no files remaining");
                return 1;
            }
            return status;
        }
        context.Flush();
        return FollowFiles(context, followFiles, followMode, retry, headers, lastOutputName,
                           sleepInterval, pidGiven, pid, status);
    }

private:
    static void PrintHeader(BuiltinContext& context, const std::string& shownName, bool& printedHeader) {
        std::string text = printedHeader ? "\n" : "";
        text += "==> " + shownName + " <==\n";
        context.Out(text);
        printedHeader = true;
    }

    // The follow loop: each round asks every file what has become of it, then
    // sleeps --sleep-interval, in slices short enough that a stop is noticed
    // at once. Ends when asked to stop (the process reports 143), when the
    // --pid process ends, or when the last file is given up on.
    static int FollowFiles(BuiltinContext& context, std::vector<TailFile>& files,
                           TailFollow mode, bool retry, bool headers,
                           std::string& lastOutputName, double interval,
                           bool pidGiven, uint64_t pid, int status) {
        while (true) {
            if (context.StopRequested()) {
                return 1;
            }
            for (size_t i = 0; i < files.size(); ) {
                if (!FollowOneRound(context, files[i], mode, retry, headers, lastOutputName)) {
                    files.erase(files.begin() + i);
                    continue;
                }
                ++i;
            }
            if (files.empty()) {
                context.Error("no files remaining");
                return 1;
            }
            if (pidGiven && !PidAlive(context, pid)) {
                // The process is gone: one last read of what its death may
                // have appended, then done.
                for (auto& file : files) {
                    if (!DumpNewData(context, file, headers, lastOutputName)) {
                        return 1;
                    }
                }
                return status;
            }
            SleepInterval(context, interval);
        }
    }

    // One round for one file: reopen it on truncation or (name mode) on
    // disappearance and reappearance, and print whatever is new. Returns
    // false when the file is given up on and must leave the follow list.
    static bool FollowOneRound(BuiltinContext& context, TailFile& file, TailFollow mode,
                               bool retry, bool headers, std::string& lastOutputName) {
        IFileIO& io = context.IO();
        FileStatus stat;
        const bool exists = io.Stat(file.name, stat) == 0;
        if (mode == TailFollow::Name) {
            if (!exists) {
                if (file.descriptor) {
                    if (!file.reportedInaccessible) {
                        context.Error(ShellEscapeQuoted(file.name, /*always=*/true)
                            + " has become inaccessible: No such file or directory");
                        file.reportedInaccessible = true;
                    }
                    file.descriptor.reset();
                    if (!retry) {
                        return false;
                    }
                }
                return true;  // --retry keeps watching for it
            }
            if (!file.descriptor) {
                // It has appeared (or reappeared): follow it from its start.
                context.Error(ShellEscapeQuoted(file.name, /*always=*/true)
                    + " has appeared;  following new file");
                file.descriptor = io.OpenFile(file.name, kFileOpenReadOnly, kFileCreateMode);
                file.position = 0;
                file.reportedInaccessible = false;
                if (!file.descriptor) {
                    return retry;
                }
            } else if (stat.size < file.position) {
                if (!ReopenTruncated(context, io, file)) {
                    return retry;
                }
            }
        } else if (!file.descriptor) {
            // Descriptor mode: a file that never opened is only watched
            // under --retry (its initial open retrying for ever).
            if (!retry) {
                return false;
            }
            file.descriptor = io.OpenFile(file.name, kFileOpenReadOnly, kFileCreateMode);
            if (!file.descriptor) {
                return true;
            }
            file.position = 0;
        } else if (exists && stat.size < file.position) {
            // The path's file is gone: keep reading the descriptor, as GNU
            // does -- it is the file that was named, wherever it moved.
            if (!ReopenTruncated(context, io, file)) {
                return retry;
            }
        }
        return DumpNewData(context, file, headers, lastOutputName);
    }

    // A file that shrank: the tail of the new content starts at its start.
    static bool ReopenTruncated(BuiltinContext& context, IFileIO& io, TailFile& file) {
        context.Error(ShellEscapeQuoted(file.name) + ": file truncated");
        file.descriptor.reset();
        file.descriptor = io.OpenFile(file.name, kFileOpenReadOnly, kFileCreateMode);
        file.position = 0;
        return file.descriptor != nullptr;
    }

    static void SleepInterval(BuiltinContext& context, double seconds) {
        using clock = std::chrono::steady_clock;
        const auto end = clock::now()
            + std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(seconds));
        while (true) {
            if (context.StopRequested()) {
                return;
            }
            auto remaining = end - clock::now();
            if (remaining <= std::chrono::steady_clock::duration::zero()) {
                return;
            }
            if (remaining > std::chrono::milliseconds(10)) {
                remaining = std::chrono::milliseconds(10);
            }
            std::this_thread::sleep_for(remaining);
        }
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateTailCommand() {
    return std::make_shared<TailCommand>();
}

} // namespace Haisos