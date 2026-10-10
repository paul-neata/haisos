#include "BuiltinCommand.h"
#include "BuiltinPrompt.h"
#include "BuiltinRemove.h"
#include "BuiltinText.h"
#include <string>
#include <vector>

namespace Haisos {

namespace {

constexpr int kOptionForce = 1;
constexpr int kOptionInteractiveAlways = 2;  // -i
constexpr int kOptionInteractiveOnce = 3;    // -I
constexpr int kOptionInteractiveWord = 4;    // --interactive[=WHEN]
constexpr int kOptionNoPreserveRoot = 5;
constexpr int kOptionPreserveRoot = 6;       // --preserve-root[=all]
constexpr int kOptionRecursive = 7;           // -r and -R
constexpr int kOptionDir = 8;                 // -d
constexpr int kOptionVerbose = 9;

class RmCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "rm"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'f', "force", kOptionForce, BuiltinArgument::None, "", "ignore nonexistent files and arguments, never prompt"},
            {'i', "", kOptionInteractiveAlways, BuiltinArgument::None, "", "prompt before every removal"},
            {'I', "", kOptionInteractiveOnce, BuiltinArgument::None, "", "prompt once before removing more than three files, or when removing recursively"},
            {0, "interactive", kOptionInteractiveWord, BuiltinArgument::Optional, "WHEN", "prompt according to WHEN: never, once (-I), or always (-i); without WHEN, prompt always"},
            {0, "one-file-system", kBuiltinNotTreated},
            {0, "no-preserve-root", kOptionNoPreserveRoot, BuiltinArgument::None, "", "do not treat '/' specially"},
            {0, "preserve-root", kOptionPreserveRoot, BuiltinArgument::Optional, "all", "do not remove '/' (default)"},
            {'r', "recursive", kOptionRecursive, BuiltinArgument::None, "", "remove directories and their contents recursively"},
            {'R', "", kOptionRecursive, BuiltinArgument::None, "", "same as -r"},
            {'d', "dir", kOptionDir, BuiltinArgument::None, "", "remove empty directories"},
            {'v', "verbose", kOptionVerbose, BuiltinArgument::None, "", "explain what is being done"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "remove files or directories",
            {"rm [OPTION]... [FILE]..."},
            "Entries of a directory are removed in name order, not the disk's.\n"
            "A failed removal of an empty directory is reported as 'Device or resource busy':\n"
            "no reason is known (a read-only filesystem included).\n"
            "--preserve-root=all is taken as --preserve-root."};
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }

        bool interactive = false;
        bool promptOnce = false;
        RemoveOptions options;
        for (const auto& option : parsed->options) {
            switch (option.id) {
                // GNU keeps one interactive setting, the last option that
                // named one winning; -f takes part too.
                case kOptionForce:
                    interactive = false;
                    promptOnce = false;
                    options.ignoreMissing = true;
                    break;
                case kOptionInteractiveAlways:
                    interactive = true;
                    promptOnce = false;
                    break;
                case kOptionInteractiveOnce:
                    interactive = false;
                    promptOnce = true;
                    break;
                case kOptionInteractiveWord: {
                    if (!option.hasArgument) {
                        interactive = true;
                        promptOnce = false;
                        break;
                    }
                    static const std::vector<ArgChoice> kWhenWords = {
                        {"never", 0},
                        {"no", 0},
                        {"none", 0},
                        {"once", 1},
                        {"always", 2},
                        {"yes", 2},
                    };
                    const auto matched = ArgMatch(context, "--interactive", option.argument, kWhenWords);
                    if (!matched) {
                        return 1;
                    }
                    if (*matched == 0) {
                        interactive = false;
                        promptOnce = false;
                    } else if (*matched == 1) {
                        interactive = false;
                        promptOnce = true;
                    } else {
                        interactive = true;
                        promptOnce = false;
                    }
                    break;
                }
                case kOptionNoPreserveRoot:
                    options.preserveRoot = false;
                    break;
                case kOptionPreserveRoot:
                    options.preserveRoot = true;
                    if (option.hasArgument) {
                        static const std::vector<ArgChoice> kAllWords = {{"all", 0}};
                        if (!ArgMatch(context, "--preserve-root", option.argument, kAllWords)) {
                            return 1;
                        }
                        context.NotTreated("--preserve-root=all");
                    }
                    break;
                case kOptionRecursive: options.recursive = true; break;
                case kOptionDir: options.emptyDirectories = true; break;
                case kOptionVerbose: options.verbose = true; break;
                default: break;  // not treated (already reported)
            }
        }

        if (parsed->operands.empty()) {
            if (options.ignoreMissing) {
                return 0;
            }
            context.Error("missing operand");
            context.TryHelp();
            return 1;
        }

        options.interactive = interactive;
        BuiltinPrompt prompt(context);
        // -I: one question in front of everything, for more than three
        // operands or a recursive removal; a no removes nothing at all.
        if (promptOnce && (parsed->operands.size() > 3 || options.recursive)) {
            const std::string what = parsed->operands.size() == 1 ? "argument" : "arguments";
            std::string question = context.Name() + ": remove "
                + std::to_string(parsed->operands.size()) + " " + what;
            if (options.recursive) {
                question += " recursively";
            }
            if (!prompt.Ask(question + "? ")) {
                return 0;
            }
        }

        int status = 0;
        for (const auto& operand : parsed->operands) {
            if (!RemoveOperand(context, &prompt, operand, options)) {
                status = 1;
            }
        }
        return status;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateRmCommand() {
    return std::make_shared<RmCommand>();
}

} // namespace Haisos