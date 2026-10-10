#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "BuiltinRunProgram.h"
#include "BuiltinText.h"
#include "commands/awk/AwkError.h"
#include "commands/awk/AwkStreams.h"
#include "interfaces/IFileIO.h"
#include "interfaces/IProcess.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/libheaders/ExitCodes.h"

namespace Haisos::Awk {
namespace {

// The names of awk's own standard streams (gawk --posix opens the real
// devices; Haisos keeps them awk's own, a documented exception).
inline bool IsStandardOutputName(const std::string& name) {
    return name == "/dev/stdout" || name == "-" || name == "/dev/stderr";
}

// Why opening |name| for a redirection failed, gawk's wording: found through
// Stat, the parent of the resolved path included -- a directory there is "Is a
// directory", something else there "Permission denied", a parent that is not
// an existing directory "No such file or directory", anything else again
// "Permission denied".
std::string RedirectFailureReason(BuiltinContext& context, const std::string& name) {
    FileStatus status;
    if (context.IO().Stat(name, status) == 0) {
        return status.type == DirectoryEntryType::Dir ? "Is a directory" : "Permission denied";
    }
    const std::string resolved = context.IO().ResolvePath(name);
    const size_t slash = resolved.rfind('/');
    const std::string parent = slash == 0 ? "/" : resolved.substr(0, slash);
    if (context.IO().Stat(parent, status) != 0 || status.type != DirectoryEntryType::Dir) {
        return "No such file or directory";
    }
    return "Permission denied";
}

} // namespace

std::optional<std::string> FindAwkShell(BuiltinContext& context) {
    return FindProgramInPath(context, "hsh");
}

AwkStreams::AwkStreams(BuiltinContext& context) : m_context(context) {}

// --- writing ---

void AwkStreams::Write(RedirectKind redirect, const std::string& name, const std::string& bytes,
                       const std::string& statement) {
    const AwkStreamKind kind =
        redirect == RedirectKind::Pipe ? AwkStreamKind::OutputPipe : AwkStreamKind::OutputFile;
    Stream* stream = Find(kind, name);
    if (stream == nullptr) {
        stream = &OpenOutput(redirect, name);
    }
    if (stream->descriptor == nullptr) {
        // A standard output name: awk's own stream, written at once.
        if (name == "/dev/stderr") {
            m_context.ErrorText(bytes);
        } else {
            m_context.Out(bytes);
        }
        return;
    }
    stream->buffer += bytes;
    if (stream->buffer.size() >= kBuiltinOutBufferSize) {
        WriteOut(*stream, statement);
    }
}

AwkStreams::Stream& AwkStreams::OpenOutput(RedirectKind redirect, const std::string& name) {
    if (redirect != RedirectKind::Pipe && IsStandardOutputName(name)) {
        // Registered like any stream, so close() and fflush() find them; their
        // bytes go through the BuiltinContext, sharing awk's own order. Only
        // `>' and `>>' name them: `| "/dev/stderr"' is a command, as in gawk.
        m_streams.push_back(
            Stream{name, AwkStreamKind::OutputFile, nullptr, "", nullptr, nullptr, false, false});
        return m_streams.back();
    }
    if (redirect == RedirectKind::Pipe) {
        // awk's output so far must be out before the command writes to the
        // same descriptors.
        FlushAll();
        const std::optional<std::string> shell = FindAwkShell(m_context);
        const std::optional<std::pair<int, int>> slots = m_context.IO().CreatePipe();
        if (!slots) {
            throw AwkFatal("cannot open pipe `" + name + "': Too many open files");
        }
        std::shared_ptr<IFileDescriptor> readEnd = m_context.IO().GetDescriptor(slots->first);
        std::shared_ptr<IFileDescriptor> writeEnd = m_context.IO().GetDescriptor(slots->second);
        m_context.IO().CloseDescriptor(slots->first);
        m_context.IO().CloseDescriptor(slots->second);
        std::shared_ptr<IProcess> child;
        if (shell) {
            RunProgramOptions options;
            options.stdIn = readEnd;   // the command's output: awk's own stdout and stderr
            child = StartProgram(m_context, *shell, {"-c", name}, options);
        }
        // awk keeps no copy of the read end: the command must find the end of
        // its input when awk's write end goes.
        readEnd.reset();
        if (!child) {
            throw AwkFatal("cannot open pipe `" + name + "': No such file or directory");
        }
        m_streams.push_back(Stream{name, AwkStreamKind::OutputPipe, std::move(writeEnd), "",
                                    nullptr, std::move(child), false, false});
        return m_streams.back();
    }
    // `>' or `>>': one file stream per name, opened with this operator's mode
    // -- a stream opened by `>' and written again by `>>' is not reopened.
    const int flags = redirect == RedirectKind::Append ? kFileOpenWriteCreateAppend
                                                       : kFileOpenWriteCreateTruncate;
    std::shared_ptr<IFileDescriptor> file = m_context.IO().OpenFile(name, flags, kFileCreateMode);
    if (!file) {
        throw AwkFatal("cannot redirect to `" + name + "': " + RedirectFailureReason(m_context, name));
    }
    m_streams.push_back(Stream{name, AwkStreamKind::OutputFile, std::move(file), "", nullptr,
                                nullptr, false, false});
    return m_streams.back();
}

int AwkStreams::WriteOut(Stream& stream, const std::string& statement) {
    if (stream.descriptor == nullptr || stream.buffer.empty() || stream.writeFailed) {
        return 0;
    }
    const ssize_t result = WriteFully(*stream.descriptor, stream.buffer);
    stream.buffer.clear();
    if (result >= 0) {
        return 0;
    }
    if (result == kIOInterrupted) {
        return 0;   // stopped: quietly, the stream unmarked
    }
    stream.writeFailed = true;   // never written to again
    if (statement.empty()) {
        return -1;
    }
    // gawk, --posix or not, reports a broken redirected pipe as its own fatal
    // error (status 2) -- not a quiet 141, which is awk's standard output's.
    throw AwkFatal(statement + " to \"" + stream.name + "\" failed: " +
                   (result == kIOBrokenPipe ? "Broken pipe" : "Input/output error"));
}

// --- reading ---

int AwkStreams::ReadRecord(AwkStreamKind kind, const std::string& name, const std::string& rs,
                           std::string& record) {
    Stream* stream = Find(kind, name);
    if (stream == nullptr) {
        stream = OpenInput(kind, name);
        if (stream == nullptr) {
            return -1;
        }
    }
    if (stream->atEnd) {
        return 0;   // the end stays the end until close()
    }
    const RecordReadResult result = stream->reader->Next(rs, record);
    switch (result) {
        case RecordReadResult::Record:
            return 1;
        case RecordReadResult::End:
            stream->atEnd = true;
            return 0;
        case RecordReadResult::Stopped:
            return 0;   // the interpreter's ThrowIfStopped unwinds right after
        case RecordReadResult::Error:
            return -1;
    }
    return -1;
}

AwkStreams::Stream* AwkStreams::OpenInput(AwkStreamKind kind, const std::string& name) {
    std::shared_ptr<IFileDescriptor> input;
    std::shared_ptr<IProcess> child;
    if (kind == AwkStreamKind::InputFile) {
        if (name == "-" || name == "/dev/stdin") {
            // awk's own standard input. Each name is a stream of its own with
            // its own reader (a reader reads ahead, so mixing the two -- or the
            // main input from standard input -- is unspecified, as in gawk).
            input = m_context.IO().GetDescriptor(IFileIO::kStdIn);
        } else {
            InputOpenFailure failure = InputOpenFailure::None;
            input = OpenInputOperand(m_context, name, failure);
        }
        if (!input) {
            return nullptr;   // getline's quiet -1
        }
    } else {
        FlushAll();
        const std::optional<std::string> shell = FindAwkShell(m_context);
        const std::optional<std::pair<int, int>> slots = m_context.IO().CreatePipe();
        if (!slots) {
            return nullptr;
        }
        std::shared_ptr<IFileDescriptor> readEnd = m_context.IO().GetDescriptor(slots->first);
        std::shared_ptr<IFileDescriptor> writeEnd = m_context.IO().GetDescriptor(slots->second);
        m_context.IO().CloseDescriptor(slots->first);
        m_context.IO().CloseDescriptor(slots->second);
        if (shell) {
            RunProgramOptions options;
            options.stdOut = writeEnd;   // the command reads awk's own stdin and stderr
            child = StartProgram(m_context, *shell, {"-c", name}, options);
        }
        // awk keeps no copy of the write end: the read must find its end when
        // the command is done.
        writeEnd.reset();
        if (!child) {
            return nullptr;
        }
        input = std::move(readEnd);
    }
    m_streams.push_back(Stream{name, kind, input, "", std::make_unique<RecordReader>(m_context, input),
                                std::move(child), false, false});
    return &m_streams.back();
}

// --- close, flush, system ---

int AwkStreams::Close(const std::string& name) {
    // The most recently opened stream of that name, whatever its kind -- one
    // per call, so a second close of the name closes the one opened before it.
    for (size_t i = m_streams.size(); i > 0; --i) {
        if (m_streams[i - 1].name == name) {
            const int status = CloseOne(m_streams[i - 1], /*flushStdoutFirst=*/true);
            m_streams.erase(m_streams.begin() + static_cast<std::ptrdiff_t>(i - 1));
            return status;
        }
    }
    return -1;
}

int AwkStreams::CloseOne(Stream& stream, bool flushStdoutFirst) {
    switch (stream.kind) {
        case AwkStreamKind::OutputFile:
            if (stream.descriptor == nullptr) {
                // A standard output name: its bytes are already out.
                if (flushStdoutFirst) {
                    m_context.Flush();
                }
                return 0;
            }
            return WriteOut(stream, "");
        case AwkStreamKind::OutputPipe: {
            // awk's own stdout written out first (gawk's `print "x" | "cat";
            // print "y"; close("cat")' prints y before x), then this stream's
            // bytes, then the command waited for: its output must not overtake
            // awk's.
            if (flushStdoutFirst) {
                m_context.Flush();
            }
            // A failed last write (the command gone before its input's end)
            // still reports the command's own status, as gawk's does: the
            // write's fate is not close()'s.
            (void)WriteOut(stream, "");
            stream.descriptor.reset();   // the command finds the end of its input
            return MapCommandStatus(WaitForProgram(m_context, *stream.child));
        }
        case AwkStreamKind::InputFile:
            stream.reader.reset();
            stream.descriptor.reset();
            return 0;
        case AwkStreamKind::InputPipe:
            stream.reader.reset();
            stream.descriptor.reset();
            return MapCommandStatus(WaitForProgram(m_context, *stream.child));
    }
    return -1;
}

int AwkStreams::Flush(const std::string* name) {
    if (name == nullptr || name->empty()) {
        FlushAll();
        return 0;
    }
    // The standard output names flush without being opened, as gawk's do.
    if (*name == "/dev/stdout") {
        m_context.Flush();
        return 0;
    }
    if (*name == "/dev/stderr") {
        return 0;
    }
    bool seen = false;
    for (Stream& stream : m_streams) {
        if (stream.name == *name &&
            (stream.kind == AwkStreamKind::OutputFile || stream.kind == AwkStreamKind::OutputPipe)) {
            seen = true;
            if (WriteOut(stream, "") < 0) {
                return -1;
            }
        }
    }
    return seen ? 0 : -1;
}

void AwkStreams::FlushAll() {
    m_context.Flush();
    for (Stream& stream : m_streams) {
        if (stream.kind == AwkStreamKind::OutputFile || stream.kind == AwkStreamKind::OutputPipe) {
            WriteOut(stream, "");   // a failure only marks the stream
        }
    }
}

int AwkStreams::RunSystem(const std::string& command) {
    FlushAll();
    const std::optional<std::string> shell = FindAwkShell(m_context);
    if (!shell) {
        return 127;
    }
    // The command gets awk's own standard streams, environment and working
    // directory -- `hsh -c <command>', even for the empty command.
    return MapCommandStatus(RunProgramAndWait(m_context, *shell, {"-c", command}));
}

int AwkStreams::MapCommandStatus(int code) {
    if (code == kExitCodeStopped) {
        return 271;   // 256 + SIGTERM (15): a command stopped
    }
    if (code == kExitCodeBrokenPipe) {
        return 269;   // 256 + SIGPIPE (13): a command lost its reader
    }
    return code;
}

void AwkStreams::CloseAll() {
    // The most recently opened first, so each command's output follows the
    // order gawk's does (`print "1" | "sort -r"; print "2" | "sort"; print "3"
    // | "cat"' prints 3 2 1). awk's own stdout is not written out first: at
    // the end gawk finishes its pipes before its buffered stdout, so that is
    // left to the BuiltinContext.
    while (!m_streams.empty()) {
        CloseOne(m_streams.back(), /*flushStdoutFirst=*/false);
        m_streams.pop_back();
    }
}

AwkStreams::Stream* AwkStreams::Find(AwkStreamKind kind, const std::string& name) {
    for (Stream& stream : m_streams) {
        if (stream.kind == kind && stream.name == name) {
            return &stream;
        }
    }
    return nullptr;
}

} // namespace Haisos::Awk