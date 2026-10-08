#include "BuiltinCommand.h"
#include <string>
#include <vector>

namespace Haisos {

namespace {

// true and false are one shape: every argument ignored except --help and
// --version as the sole one, as GNU's (which print it and then exit with
// their own status -- false's --help exits 1).
class TrueCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "true"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options;
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "do nothing, successfully",
            {"true [ignored command line arguments]", "true OPTION"},
            "--help and --version count only as the sole argument.",
        };
    }

    int Run(BuiltinContext& context) override {
        const auto& args = context.Args();
        if (args.size() == 1 && args[0] == "--help") {
            context.Out(BuiltinHelpText(*this));
            return 0;
        }
        if (args.size() == 1 && args[0] == "--version") {
            context.Out(BuiltinVersionText(*this));
            return 0;
        }
        return 0;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateTrueCommand() {
    return std::make_shared<TrueCommand>();
}

} // namespace Haisos