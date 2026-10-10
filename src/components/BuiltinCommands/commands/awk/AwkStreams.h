#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "commands/awk/AwkAst.h"
#include "commands/awk/AwkInput.h"
#include "interfaces/IFileDescriptor.h"

namespace Haisos {

class IProcess;

namespace Awk {

// What one redirection opens. `>' and `>>' name the same kind of stream (one
// open file, whichever operator opened it first); a pipe is its own kind,
// output and input each.
enum class AwkStreamKind { OutputFile, OutputPipe, InputFile, InputPipe };

// The shell every command runs in: hsh found through PATH
// (FindProgramInPath), or nullopt when there is none.
std::optional<std::string> FindAwkShell(BuiltinContext& context);

// awk's open streams: the files and pipes its redirections and getlines
// opened, close() and fflush() over them, and system(). Streams are found by
// name and kind; close() finds by name alone, one stream per call, the most
// recently opened first. Nothing here holds an OS, a filesystem or a pipe
// service: files go through context.IO(), pipes only through
// context.IO().CreatePipe(), commands only through StartProgram and
// RunProgramAndWait -- the process is the only door out, and a pipe end awk
// does not use is released at once.
class AwkStreams {
public:
    explicit AwkStreams(BuiltinContext& context);

    // print/printf with a redirection (File ">", Append ">>", Pipe "|"):
    // |bytes| to the stream |name|, opened first when it is not open, and
    // flushed before a command starts. |statement| is "print" or "printf",
    // for a failed write's error text. Throws AwkFatal.
    void Write(RedirectKind redirect, const std::string& name, const std::string& bytes,
               const std::string& statement);

    // getline < file (InputFile) or command | getline (InputPipe): 1 with
    // |record| set, 0 at the end (and when stopped), -1 when it cannot be
    // opened or read. |rs| is RS now. Never throws.
    int ReadRecord(AwkStreamKind kind, const std::string& name, const std::string& rs,
                   std::string& record);

    // close(name): the most recently opened stream of that name, whatever its
    // kind, one per call. 0 for a file (and a standard stream), the command's
    // status as RunSystem maps it for a pipe of either direction, -1 when
    // nothing of that name is open or its output could not be written out.
    int Close(const std::string& name);
    // fflush(name), null: everything. 0, or -1 when |name| is no open output.
    // The standard output names work without being opened, as gawk's do.
    int Flush(const std::string* name);
    // awk's own standard output and every output stream written out: before
    // any command starts, and by fflush().
    void FlushAll();
    // system(command): the command run by hsh (`hsh -c <command>'), the
    // status as plain gawk 5 reports it (below, in the .cpp). 127 without an
    // hsh to run it in.
    int RunSystem(const std::string& command);
    // Every stream closed, the most recently opened first, pipes waited for:
    // on every way out of the run. awk's own standard output is left to the
    // BuiltinContext. Never throws.
    void CloseAll();

private:
    struct Stream {
        std::string name;
        AwkStreamKind kind;
        // Null for the standard output names (their bytes go through the
        // BuiltinContext); an output's write end or an input's read end.
        std::shared_ptr<IFileDescriptor> descriptor;
        // An output's bytes not yet written, emptied by WriteOut.
        std::string buffer;
        // An input's record reader, over its descriptor.
        std::unique_ptr<RecordReader> reader;
        // A pipe's command, output and input alike.
        std::shared_ptr<IProcess> child;
        bool atEnd = false;        // an input past its last record
        bool writeFailed = false;  // an output whose write already failed
    };

    // The stream of |name| and |kind|, or null when it is not open.
    Stream* Find(AwkStreamKind kind, const std::string& name);
    // The output stream of |name|, opened: a file through context.IO() with
    // |redirect|'s open mode (a failure is AwkFatal, gawk's wording), a pipe
    // whose command could not be started likewise. The standard output names
    // are registered and written through the BuiltinContext.
    Stream& OpenOutput(RedirectKind redirect, const std::string& name);
    // The input stream of |name|, opened and with its reader: a file through
    // OpenInputOperand, a pipe as OpenOutput makes one, with the command's
    // output. Null when it cannot be opened -- the caller's quiet -1.
    Stream* OpenInput(AwkStreamKind kind, const std::string& name);
    // |stream|'s buffer written out, looped until every byte is through: 0,
    // or -1 when it cannot be. |statement| as Write's: empty, a failed write
    // only marks the stream; a named one is AwkFatal, gawk's fatal for a
    // broken redirected pipe (a stopped write ends quietly, 0).
    int WriteOut(Stream& stream, const std::string& statement);
    // Closes |stream| as Close does, |flushStdoutFirst| false at the end of
    // the run (awk's stdout is written after the pipes finish). Never throws.
    int CloseOne(Stream& stream, bool flushStdoutFirst);
    // What a command reports, as plain gawk 5 does and gawk --posix does not:
    // the exit code; 256 + the signal for a command stopped (Haisos's
    // SIGTERM, 143) or broken-piped (141, SIGPIPE) -- inside the shell too,
    // where gawk, whose shell outlives the command, sees the shell's own code.
    static int MapCommandStatus(int code);

    BuiltinContext& m_context;
    std::vector<Stream> m_streams;   // in opening order
};

} // namespace Awk

} // namespace Haisos