#include <chrono>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "BuiltinCommand.h"
#include "BuiltinText.h"
#include "commands/sed/SedExecutor.h"
#include "commands/sed/SedParser.h"
#include "interfaces/IFileIO.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {
namespace {

enum SedOption {
    kQuiet = 1,     // -n, --quiet
    kExpression,    // -e, --expression
    kFile,          // -f, --file
    kExtended,      // -E, -r, --regexp-extended
    kSeparate,      // -s, --separate
    kInPlace,       // -i, --in-place
    kLineLength,    // -l, --line-length
    kUnbuffered,    // -u, --unbuffered
    kNullData,      // -z, --null-data, --zero-terminated
    kFollowSymlinks,
    kSandbox,
};

class SedCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "sed"; }
    std::string Version() const override { return "1.1.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'n', "quiet", kQuiet, BuiltinArgument::None, "",
             "suppress automatic printing of pattern space"},
            {0, "silent", kQuiet, BuiltinArgument::None, "", "", true},
            {'e', "expression", kExpression, BuiltinArgument::Required, "script",
             "add the script to the commands to be executed"},
            {'f', "file", kFile, BuiltinArgument::Required, "script-file",
             "add the contents of script-file to the commands to be executed"},
            {'E', "regexp-extended", kExtended, BuiltinArgument::None, "",
             "use extended regular expressions in the script"},
            {'r', "", kExtended, BuiltinArgument::None, "", "", true},
            {'s', "separate", kSeparate, BuiltinArgument::None, "",
             "consider files as separate rather than as a single continuous long stream"},
            {'i', "in-place", kInPlace, BuiltinArgument::OptionalAttached, "SUFFIX",
             "edit files in place (makes backup if SUFFIX supplied)"},
            {'l', "line-length", kLineLength, BuiltinArgument::Required, "N",
             "specify the desired line-wrap length for the `l' command"},
            {'u', "unbuffered", kUnbuffered, BuiltinArgument::None, "",
             "load minimum amounts of data from the input files and flush the output buffers more often"},
            {'z', "null-data", kNullData, BuiltinArgument::None, "",
             "separate lines by NUL characters"},
            {0, "zero-terminated", kNullData, BuiltinArgument::None, "", "", true},
            {0, "follow-symlinks", kFollowSymlinks, BuiltinArgument::None, "",
             "follow symlinks when processing in place"},
            {0, "sandbox", kSandbox, BuiltinArgument::None, "",
             "operate in sandbox mode (disable e/r/w commands)"},
            {0, "debug", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "posix", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        BuiltinHelp help;
        help.summary = "stream editor for filtering and transforming text";
        help.usage.push_back("sed [OPTION]... {script-only-if-no-other-script} [input-file]...");
        help.notes =
            "Every command of GNU sed 4.9 but e (which HaisosOS does not treat, reported on --help "
            "as such): the advanced commands N D P h H g G x, b t T, a i c (GNU's one-line and "
            "classic \\ forms), r R w W (with /dev/stdout and /dev/stderr), s///w, y, l, z, F and "
            "v -- with -i (through a temp file), -l, -u, -z and --sandbox. --posix and --debug "
            "are not treated.";
        return help;
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, 1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }
        std::vector<Sed::ScriptPiece> pieces;
        bool extended = false;
        bool separate = false;
        bool quiet = false;
        bool sandbox = false;
        bool unbuffered = false;
        char delimiter = '\n';
        int lineLength = 70;
        std::string inPlaceSuffix;
        bool inPlace = false;
        for (const auto& option : parsed->options) {
            switch (option.id) {
                case kQuiet:
                    quiet = true;
                    break;
                case kExpression:
                    pieces.push_back(Sed::ScriptPiece{option.argument, false, ""});
                    break;
                case kFile: {
                    std::string content;
                    if (!ReadScriptFile(context, option.argument, content)) {
                        return 4;  // GNU exits 4 when it cannot read a script file
                    }
                    pieces.push_back(Sed::ScriptPiece{content, true, option.argument});
                    break;
                }
                case kExtended:
                    extended = true;
                    break;
                case kSeparate:
                    separate = true;
                    break;
                case kInPlace:
                    inPlace = true;
                    inPlaceSuffix = option.argument;
                    break;
                case kLineLength:
                    // GNU's atoi: anything not a number is 0 (never wrap).
                    lineLength = std::atoi(option.argument.c_str());
                    if (lineLength < 0) {
                        lineLength = 0;
                    }
                    break;
                case kUnbuffered:
                    unbuffered = true;
                    break;
                case kNullData:
                    delimiter = '\0';
                    break;
                case kSandbox:
                    sandbox = true;
                    break;
                default:
                    break;
            }
        }
        std::vector<std::string> inputs = parsed->operands;
        if (pieces.empty()) {
            if (inputs.empty()) {
                context.ErrorText(BuiltinHelpText(*this));
                return 1;
            }
            // The script operand is added last, as GNU does.
            pieces.push_back(Sed::ScriptPiece{inputs.front(), false, ""});
            inputs.erase(inputs.begin());
        }
        Sed::Script script;
        if (!Sed::ParseScript(context, pieces, extended, sandbox, script)) {
            return script.parseErrorStatus;
        }
        if (inPlace) {
            if (inputs.empty()) {
                context.Error("no input files");
                return 4;
            }
            separate = true;  // GNU's -i implies -s
        } else if (inputs.empty()) {
            inputs.push_back("-");  // standard input
        }
        Sed::ExecSettings settings;
        settings.quiet = quiet;
        settings.separate = separate;
        settings.delimiter = delimiter;
        settings.lineLength = lineLength;
        settings.unbuffered = unbuffered;
        if (inPlace) {
            return RunInPlace(context, script, inputs, inPlaceSuffix, settings);
        }
        return Sed::RunScript(context, script, inputs, settings);
    }

private:
    // -i: each file runs through the script alone, the output gathered in a
    // temp file next to the original that is then renamed over it (the
    // original first renamed onto a backup when a SUFFIX is given).
    static int RunInPlace(BuiltinContext& context, Sed::Script& script, const std::vector<std::string>& inputs,
                          const std::string& suffix, const Sed::ExecSettings& settings)
    {
        int status = 0;
        for (const std::string& name : inputs) {
            FileStatus fileStat;
            if (context.IO().Stat(name, fileStat) != 0) {
                context.Error("can't read " + name + ": No such file or directory");
                if (status == 0) {
                    status = 2;
                }
                continue;
            }
            if (fileStat.type != DirectoryEntryType::File) {
                context.Error("couldn't edit " + name + ": not a regular file");
                return 4;  // GNU stops at once
            }
            const std::string temp = TempFileName(context, name);
            auto output = context.IO().OpenFile(temp, kFileOpenWriteCreateTruncate);
            if (!output) {
                context.Error("couldn't open file " + temp + ": Permission denied");
                return 4;
            }
            Sed::ExecSettings fileSettings = settings;
            fileSettings.inPlaceOutput = output;
            const int run = Sed::RunScript(context, script, {name}, fileSettings);
            if (run == 4) {
                // GNU's read error aborts everything: the original stays as
                // it was, the temp goes away, no file after this one runs.
                context.IO().RemoveFile(temp);
                return 4;
            }
            if (run != 0 && status == 0) {
                status = run;  // q's code, which still renames, as GNU's does
            }
            // The original moves onto the backup first, so nothing is lost
            // when the temp cannot be moved over it.
            if (!suffix.empty()) {
                std::string backup = suffix;
                const size_t star = backup.find('*');
                if (star == std::string::npos) {
                    backup = name + suffix;
                } else {
                    size_t at = 0;
                    while ((at = backup.find('*', at)) != std::string::npos) {
                        backup.replace(at, 1, name);
                        at += name.size();
                    }
                }
                if (context.IO().Rename(name, backup) != 0) {
                    context.Error("cannot rename " + name + ": No such file or directory");
                    context.IO().RemoveFile(temp);
                    return 4;
                }
            }
            if (context.IO().Rename(temp, name) != 0) {
                context.Error("cannot rename " + name + ": No such file or directory");
                context.IO().RemoveFile(temp);
                return 4;
            }
        }
        return status;
    }

    // A name GNU's --in-place could have taken, "sed" and six random
    // letters, in the original's directory, not one that is already there.
    static std::string TempFileName(BuiltinContext& context, const std::string& name)
    {
        static std::mt19937 generator(static_cast<uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        static const char alphabet[] =
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        const size_t slash = name.find_last_of('/');
        const std::string dir = slash == std::string::npos ? "" : name.substr(0, slash + 1);
        while (true) {
            std::string candidate = dir + "sed";
            for (int i = 0; i < 6; ++i) {
                candidate += alphabet[generator() % 62];
            }
            FileStatus fileStat;
            if (context.IO().Stat(candidate, fileStat) != 0) {
                return candidate;
            }
        }
    }

    // Reads a script file whole, "-" being standard input. A directory is
    // read as empty, as GNU reads it; anything else that cannot be opened is
    // GNU's "couldn't open file" (the caller exits 4).
    static bool ReadScriptFile(BuiltinContext& context, const std::string& name, std::string& content) {
        std::shared_ptr<IFileDescriptor> input;
        if (name == "-") {
            input = context.IO().GetDescriptor(IFileIO::kStdIn);
        } else {
            InputOpenFailure failure = InputOpenFailure::None;
            input = OpenInputOperand(context, name, failure);
            if (!input) {
                if (failure == InputOpenFailure::Directory) {
                    return true;
                }
                std::string reason = "No such file or directory";
                if (failure == InputOpenFailure::Denied) {
                    reason = "Permission denied";
                } else if (failure == InputOpenFailure::BadDescriptor) {
                    reason = "Bad file descriptor";
                }
                context.Error("couldn't open file " + name + ": " + reason);
                return false;
            }
        }
        BuiltinLineReader reader(context, *input, '\n');
        std::string line;
        bool delimited = false;
        while (reader.Next(line, delimited) == LineReadResult::Line) {
            content += line;
            if (delimited) {
                content += '\n';
            }
        }
        return true;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateSedCommand()
{
    return std::make_shared<SedCommand>();
}

} // namespace Haisos