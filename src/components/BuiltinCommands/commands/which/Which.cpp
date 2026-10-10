#include "BuiltinCommand.h"
#include "BuiltinRunProgram.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include <string>
#include <vector>

namespace Haisos {

namespace {

constexpr int kOptionAll = 1;    // -a
constexpr int kOptionSilent = 2;  // -s

class WhichCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "which"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'a', "", kOptionAll, BuiltinArgument::None, "", "print all matching pathnames of each argument"},
            {'s', "", kOptionSilent, BuiltinArgument::None, "", "silent: exit status only"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "locate a command",
            {"which [-as] args"},
            "Debian's which (debianutils), not GNU which; --help and --version\n"
            "are Haisos's (Debian's which has none).",
            // The reference is still the Linux man-pages project's which page.
        };
    }

    int Run(BuiltinContext& context) override {
        // Debian's which is no getopt command: an unknown option is reported
        // its own way, so the parse is done directly, not through BeginBuiltin.
        const ParsedBuiltinArgs parsed = ParseBuiltinArgs(context.Args(), Options());
        if (!parsed.error.empty()) {
            context.ErrorText("Illegal option " + IllegalOptionText(context.Args()) + "\n");
            context.Out("Usage: " + context.Process().Path() + " [-as] args\n");
            return 2;
        }
        for (const auto& option : parsed.options) {
            if (option.id == kBuiltinOptionHelp) {
                context.Out(BuiltinHelpText(*this));
                return 0;
            }
            if (option.id == kBuiltinOptionVersion) {
                context.Out(BuiltinVersionText(*this));
                return 0;
            }
        }

        bool all = false;
        bool silent = false;
        for (const auto& option : parsed.options) {
            if (option.id == kOptionAll) {
                all = true;
            } else if (option.id == kOptionSilent) {
                silent = true;
            }
        }
        bool anyMissed = parsed.operands.empty();
        for (const std::string& name : parsed.operands) {
            bool found = false;
            if (name.find('/') != std::string::npos) {
                // A name with a '/' is reported as it is, when a file is there.
                FileStatus status;
                if (context.IO().Stat(name, status) == 0 && status.type == DirectoryEntryType::File) {
                    if (!silent) {
                        context.Out(name + "\n");
                    }
                    found = true;
                }
            } else {
                // Otherwise every PATH entry, an empty one taken as '.'; the
                // candidate is built literally, so it is printed as built.
                for (const std::string& entry : SearchPathEntries(context)) {
                    const std::string candidate = entry.empty() ? "./" + name : entry + "/" + name;
                    FileStatus status;
                    if (context.IO().Stat(candidate, status) == 0
                        && status.type == DirectoryEntryType::File) {
                        if (!silent) {
                            context.Out(candidate + "\n");
                        }
                        found = true;
                        if (!all) {
                            break;
                        }
                    }
                }
            }
            if (!found) {
                anyMissed = true;
            }
        }
        return anyMissed ? 1 : 0;
    }

private:
    // What Debian's which calls the option the parse failed on: "-x" with the
    // option letter, "--" for an unknown long option.
    static std::string IllegalOptionText(const std::vector<std::string>& args) {
        for (const std::string& arg : args) {
            if (arg == "--") {
                break;  // everything after is an operand, no option of ours
            }
            if (arg.size() > 2 && arg.compare(0, 2, "--") == 0) {
                const std::string name = arg.substr(2);
                if (std::string("help").compare(0, name.size(), name) == 0
                    || std::string("version").compare(0, name.size(), name) == 0) {
                    continue;
                }
                return "--";
            }
            if (arg.size() > 1 && arg[0] == '-') {
                for (const char c : arg.substr(1)) {
                    if (c != 'a' && c != 's') {
                        return std::string("-") + c;
                    }
                }
            }
        }
        return "--";
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateWhichCommand() {
    return std::make_shared<WhichCommand>();
}

} // namespace Haisos