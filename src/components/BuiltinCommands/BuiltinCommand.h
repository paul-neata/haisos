#pragma once
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "interfaces/IProcess.h"

namespace Haisos {

// Whether an option takes an argument: never ("-l"), always ("-w 80",
// "--width=80"), or only when attached to a long option ("--color",
// "--color=auto").
enum class BuiltinArgument { None, Required, Optional };

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
ParsedBuiltinArgs ParseBuiltinArgs(const std::vector<std::string>& args, const std::vector<BuiltinOption>& options);

// What --help says beyond the option table.
struct BuiltinHelp {
    // A few words saying what the command does: "list directory contents".
    std::string summary;
    // Each "Usage:" form, without the word "Usage": "ls [OPTION]... [FILE]...".
    std::vector<std::string> usage;
    // Anything else the command handles that is not an option (echo's
    // escapes, say), in a few lines; may be empty.
    std::string notes;
};

class IBuiltinCommand;

// What a running builtin command is handed: the process it runs as (the only
// door out of it -- files through process.IO()), its arguments, and somewhere
// to print.
//
// There is no stdout or stderr yet, so both kinds of output go to the process's
// console. Output is buffered into lines, each one a single console Write; a
// final line without a newline is written when the command ends (which is how
// `echo -n` still shows its text).
class BuiltinContext {
public:
    BuiltinContext(
        ICurrentProcess& process,
        const IBuiltinCommand& command,
        const std::vector<std::string>& args,
        std::shared_ptr<IAgentConsole> console,
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
    // The usual tail of a usage error: "Try '<name> --help' for more information."
    void TryHelp();
    // Says, once per spelling, that each not-treated option given was not
    // acted on: "Parameter --author is not treated by HaisosOS ls v. 1.1.0".
    void ReportNotTreated(const ParsedBuiltinArgs& parsed);
    // The same for one given spelling, e.g. a value of a treated option that
    // Haisos does not handle ("--sort=version").
    void NotTreated(const std::string& spelling);

    // Set once the process has been asked to stop; a command doing a lot of
    // work (a recursive ls, say) checks it and finishes early.
    bool StopRequested() const { return m_stopRequested.load(); }

    // Writes out what is left of a line not ended by a newline.
    void Flush();

private:
    ICurrentProcess& m_process;
    std::shared_ptr<IFileIO> m_io;
    std::string m_name;
    std::string m_version;
    const std::vector<std::string>& m_args;
    std::shared_ptr<IAgentConsole> m_console;
    const std::atomic<bool>& m_stopRequested;
    std::string m_pendingLine;
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

// The start every getopt-style command shares: parses the arguments against
// command.Options(), and handles --help, --version, usage errors (reported,
// with the Try line) and not-treated options (reported, then ignored).
// Returns the parsed arguments to go on with, or nullopt when the command is
// already done, with *exitStatus set -- 0 after --help/--version,
// usageErrorStatus after a usage error.
std::optional<ParsedBuiltinArgs> BeginBuiltin(
    BuiltinContext& context, const IBuiltinCommand& command, int usageErrorStatus, int& exitStatus);

} // namespace Haisos
