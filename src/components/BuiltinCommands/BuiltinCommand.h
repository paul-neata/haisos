#pragma once
#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "interfaces/IProcess.h"

namespace Haisos {

// Whether an option takes an argument: never ("-l"), always ("-w 80",
// "--width=80"), only when attached to a long option ("--color",
// "--color=auto"), or attached to either spelling but never as the next word
// (OptionalAttached: man-db's "-Tutf8", "--troff-device=utf8"; "-T utf8" is
// -T, then the operand utf8).
enum class BuiltinArgument { None, Required, Optional, OptionalAttached };

// An id no treated option uses: it marks an option of the real command that
// Haisos recognizes but does not act on (see BuiltinContext::ReportNotTreated).
constexpr int kBuiltinNotTreated = 0;
// Option ids every command shares, added to every table by the parser.
constexpr int kBuiltinOptionHelp = -1;
constexpr int kBuiltinOptionVersion = -2;

// One option of the real command. Every option the real command accepts is
// listed, treated or not: an untreated one is still parsed -- its argument
// consumed -- so the command can say it was not acted on instead of failing as
// if it were unknown.
struct BuiltinOption {
    char shortName = 0;       // 0 if there is none
    std::string longName;     // empty if there is none
    int id = kBuiltinNotTreated;
    BuiltinArgument argument = BuiltinArgument::None;
    std::string argumentName; // shown in --help: "COLS" gives "-w, --width=COLS"
    std::string description;  // a few words, for --help; treated options only
};

struct ParsedBuiltinOption {
    int id = kBuiltinNotTreated;
    std::string argument;
    bool hasArgument = false;
    // How it was given, without any argument: "-Z" or "--context".
    std::string spelling;
};

struct ParsedBuiltinArgs {
    // In the order given, not-treated ones included.
    std::vector<ParsedBuiltinOption> options;
    std::vector<std::string> operands;
    // Set (to e.g. "invalid option -- 'x'") on the first option the real
    // command would not accept either.
    std::string error;
};

// A GNU getopt_long-style parser: short options may be clustered ("-la"), a
// long option may be abbreviated to any unambiguous prefix ("--rec"), options
// and operands may be mixed ("ls dir -l"), and "--" ends the options. A lone
// "-" is an operand. --help and --version are recognized for every command.
//
// GNU's "+" getopt mode is |stopAtFirstOperand|: options are only those before
// the first operand -- the first argument that is not an option ("-" alone
// included) and every argument after it, "--" included, become operands
// untouched. A "--" before any operand still ends the options and is dropped.
// env (and later xargs) work this way: what follows COMMAND is never options.
ParsedBuiltinArgs ParseBuiltinArgs(const std::vector<std::string>& args,
    const std::vector<BuiltinOption>& options, bool stopAtFirstOperand = false);

// What --help says beyond the option table.
struct BuiltinHelp {
    // A few words saying what the command does: "list directory contents".
    std::string summary;
    // Each "Usage:" form, without the word "Usage": "ls [OPTION]... [FILE]...".
    std::vector<std::string> usage;
    // Anything else the command handles that is not an option (echo's
    // escapes, say), in a few lines; may be empty.
    std::string notes;
    // The real command this builtin copies, when its name differs from the
    // builtin's own: "dash" for hsh. Empty: the builtin's own name. The "Based
    // on Linux <command>: <url>" line of --help names it and links its page.
    std::string basedOn;
};

class IBuiltinCommand;

// The block size at which a buffered (non-terminal) stdout is written out.
constexpr size_t kBuiltinOutBufferSize = 4096;

// What a running builtin command is handed: the process it runs as (the only
// door out of it -- files through process.IO()), its arguments, and its
// standard streams, read once at construction from the process's descriptor
// table: Out writes to descriptor 1, Error and the rest to descriptor 2.
//
// The output rule:
//   * stdout to a terminal is unbuffered: each Out(text) is written at once,
//     as one write of text (nothing is held back, so a partial line such as a
//     prompt shows immediately);
//   * stdout to anything else (a file, a pipe, a device) is block-buffered:
//     Out appends to a buffer, which is written when it reaches
//     kBuiltinOutBufferSize, before anything is written to stderr, and when the
//     command ends (~BuiltinContext flushes it) -- which is how `echo -n`
//     still shows its text;
//   * stderr is unbuffered: every message is one write.
// Output reaches its descriptor no later than the command's end, and a
// terminal never waits for a newline.
class BuiltinContext {
public:
    BuiltinContext(
        ICurrentProcess& process,
        const IBuiltinCommand& command,
        const std::vector<std::string>& args,
        const std::atomic<bool>& stopRequested);
    ~BuiltinContext();

    BuiltinContext(const BuiltinContext&) = delete;
    BuiltinContext& operator=(const BuiltinContext&) = delete;

    ICurrentProcess& Process() { return m_process; }
    IFileIO& IO() { return *m_io; }
    // The builtin's own name, the way its messages begin ("ls: ...").
    const std::string& Name() const { return m_name; }
    const std::string& Version() const { return m_version; }
    // The arguments after the command name (argv[1] onwards).
    const std::vector<std::string>& Args() const { return m_args; }

    // Standard output: text as given, newlines included.
    void Out(const std::string& text);
    // Standard error: one diagnostic line, "<name>: " prepended.
    void Error(const std::string& message);
    // Standard error, text exactly as given: no "<name>: " prefix, no newline
    // added. For the lines GNU tools print without their name ("Valid
    // arguments are:", man-db's "No manual entry for x").
    void ErrorText(const std::string& text);
    // The usual tail of a usage error: "Try '<name> --help' for more information."
    void TryHelp();
    // Whether standard output (descriptor 1) is a terminal: false when the
    // slot is empty.
    bool OutIsTerminal() const { return m_outIsTerminal; }
    // Says, once per spelling, that each not-treated option given was not
    // acted on: "Parameter --author is not treated by HaisosOS ls v. 1.1.0".
    void ReportNotTreated(const ParsedBuiltinArgs& parsed);
    // The same for one given spelling, e.g. a value of a treated option that
    // Haisos does not handle ("--sort=version").
    void NotTreated(const std::string& spelling);

    // Set once the process has been asked to stop; a command doing a lot of
    // work (a recursive ls, say) checks it and finishes early.
    bool StopRequested() const { return m_stopRequested.load(); }

    // Writes out what is left in the stdout buffer.
    void Flush();

private:
    // The one place a builtin's bytes reach a descriptor: loops over partial
    // writes until every byte is out; a null descriptor or a negative result
    // stops the loop and returns false. A kIOBrokenPipe result -- the reader of
    // the pipe is gone -- stops the process quietly with exit code 141, as
    // SIGPIPE would, on stdout and stderr alike; everything after is dropped.
    bool WriteAll(IFileDescriptor* descriptor, const std::string& bytes);

    ICurrentProcess& m_process;
    std::shared_ptr<IFileIO> m_io;
    std::string m_name;
    std::string m_version;
    const std::vector<std::string>& m_args;
    // Slots 1 and 2 of the process's table, fetched once at construction.
    std::shared_ptr<IFileDescriptor> m_out;
    std::shared_ptr<IFileDescriptor> m_err;
    bool m_outIsTerminal;
    const std::atomic<bool>& m_stopRequested;
    std::string m_outBuffer;
    // Once a write to stdout has failed, later stdout output is dropped.
    bool m_outFailed = false;
    // Once a write has hit a pipe with no reader, StopForBrokenPipe has been
    // called and the program is dying quietly: every later write, stderr's
    // included, is dropped.
    bool m_brokenPipe = false;
    std::vector<std::string> m_reportedNotTreated;
};

// One command compiled into Haisos (see IBuiltinCommands). Stateless: every
// run gets a context of its own, so one instance serves every process running
// that command.
class IBuiltinCommand {
public:
    virtual ~IBuiltinCommand() = default;
    virtual std::string Name() const = 0;
    virtual std::string Version() const = 0;
    // Every option of the real command, treated or not (--help and --version
    // are added by the parser and need not be listed).
    virtual const std::vector<BuiltinOption>& Options() const = 0;
    virtual BuiltinHelp Help() const = 0;
    // The builtin's manual page, as `man <name>` prints it: plain text, ending
    // in a newline. By default exactly its --help text (BuiltinHelpText), so
    // `man <name>` and `<name> --help` print the same; a builtin with more to
    // say (hsh) overrides it.
    virtual std::string ManPage() const;
    // Runs the command to completion and returns its exit status, 0 meaning
    // success, as the real command's would.
    virtual int Run(BuiltinContext& context) = 0;
};

// --- Shared helpers for the commands ---

// Where the real command's options are documented. One site for every
// builtin: the Linux man-pages project's plain HTML pages.
std::string BuiltinReferenceUrl(const std::string& name);

// The "--version" text: "<name> (HaisosOS builtin) <version>".
std::string BuiltinVersionText(const IBuiltinCommand& command);

// The "--help" text, the same shape for every builtin:
//
//   HaisosOS <name> version <version> - <summary>
//   Based on Linux <name>: <reference url>
//
//   Usage: <usage>...
//
//   <each treated option, with a few words>
//
//   <notes, if any>
//
//   Not treated arguments: <every untreated option, comma-separated>
//
// Only what Haisos handles is described; the last line lists the rest, or
// says "none".
std::string BuiltinHelpText(const IBuiltinCommand& command);

// A name as GNU tools print it in shell-escape quoting: as it is when no
// shell would read anything in it specially, else quoted. |always| quotes
// even a name that needs none (GNU's quoteaf, used for "cannot open 'x'"),
// otherwise only when needed (GNU's quotef and ls on a terminal).
// A name needs quoting when it is empty; or its first byte is '#' or '~'; or
// it is exactly "{" or "}"; or it holds a byte below 0x20, 0x7F, or one of
//   space ! " $ & ' ( ) * ; < = > ? [ \ ^ ` |
// Bytes 0x80 and above are kept as they are: valid UTF-8 is printable, and an
// invalid byte, which GNU would write as \NNN, is kept too.
// Quoted as GNU's shell-escape style does: 'name' when it holds neither a
// control byte nor a '; "name" when a ' but no other byte a double-quoted
// shell string would treat specially ("it's" -> '"it's"'); otherwise the
// name in '...' with each ' written '\'' and each run of control bytes
// written out of the quotes as $'\a' style escapes ("\177" for bytes without
// a letter escape) -- 'nl'$'\n''y', 'a'$'\001\002''b', 'a'$'\001'.
std::string ShellEscapeQuoted(const std::string& name, bool always = false);

// The start every getopt-style command shares: parses the arguments against
// command.Options(), and handles --help, --version, usage errors (reported,
// with the Try line) and not-treated options (reported, then ignored).
// Returns the parsed arguments to go on with, or nullopt when the command is
// already done, with *exitStatus set -- 0 after --help/--version,
// usageErrorStatus after a usage error, and parses with the given
// stopAtFirstOperand mode (see ParseBuiltinArgs).
std::optional<ParsedBuiltinArgs> BeginBuiltin(
    BuiltinContext& context, const IBuiltinCommand& command, int usageErrorStatus, int& exitStatus,
    bool stopAtFirstOperand = false);

} // namespace Haisos
