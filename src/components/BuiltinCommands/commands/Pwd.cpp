#include "BuiltinCommand.h"

namespace Haisos {

namespace {

constexpr int kOptionLogical = 1;
constexpr int kOptionPhysical = 2;

class PwdCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "pwd"; }
    std::string Version() const override { return "1.1.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        // HaisosOS has no symbolic links, so the two print the same directory.
        static const std::vector<BuiltinOption> options = {
            {'L', "logical", kOptionLogical, BuiltinArgument::None, "", "logical path (no symlinks exist)"},
            {'P', "physical", kOptionPhysical, BuiltinArgument::None, "", "physical path (the same)"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{"print name of current/working directory", {"pwd [OPTION]..."}, ""};
    }

    int Run(BuiltinContext& context) override {
        int status = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/1, status);
        if (!parsed) {
            return status;
        }
        if (!parsed->operands.empty()) {
            context.Error("ignoring non-option arguments");
        }
        context.Out(context.IO().GetCurrentDirectory() + "\n");
        return 0;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreatePwdCommand() {
    return std::make_shared<PwdCommand>();
}

} // namespace Haisos
