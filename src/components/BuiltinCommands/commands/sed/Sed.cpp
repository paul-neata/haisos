#include <memory>
#include <string>
#include <vector>

#include "BuiltinCommand.h"
#include "BuiltinText.h"
#include "commands/sed/SedExecutor.h"
#include "commands/sed/SedParser.h"
#include "interfaces/IFileIO.h"

namespace Haisos {
namespace {

enum SedOption {
    kQuiet = 1,     // -n, --quiet
    kExpression,    // -e, --expression
    kFile,          // -f, --file
    kExtended,      // -E, -r, --regexp-extended
    kSeparate,      // -s, --separate
};

class SedCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "sed"; }
    std::string Version() const override { return "1.0.0"; }

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
            // The rest of GNU sed's options, accepted and reported as not
            // treated: -i -l -u -z and the long-only ones come with the
            // advanced task.
            {'i', "in-place", kBuiltinNotTreated, BuiltinArgument::OptionalAttached, "SUFFIX", ""},
            {'l', "line-length", kBuiltinNotTreated, BuiltinArgument::Required, "N", ""},
            {'u', "unbuffered", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {'z', "null-data", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "zero-terminated", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "follow-symlinks", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "sandbox", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
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
            "The commands of GNU sed 4.9's core: s, d, p, n, q, Q, =, # and labels -- "
            "with blocks {} and every address form. The rest (a b c i r R t T w W y z ...) "
            "comes with the advanced task, and so do -i -l -u -z --sandbox and s///w.";
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
        if (!Sed::ParseScript(context, pieces, extended, script)) {
            return 1;
        }
        if (inputs.empty()) {
            inputs.push_back("-");  // standard input
        }
        Sed::ExecSettings settings;
        settings.quiet = quiet;
        settings.separate = separate;
        return Sed::RunScript(context, script, inputs, settings);
    }

private:
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