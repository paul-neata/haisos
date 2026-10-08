#include "BuiltinCommand.h"
#include <string>
#include <vector>

namespace Haisos {

namespace {

constexpr int kOptionZero = 1;

// NAME with its last component and trailing slashes removed, as GNU's
// dir_len does: the length up to the start of the last component, the
// trailing '/'s stripped, but at least the root '/' of an absolute name; "."
// when nothing is left.
std::string DirName(const std::string& name) {
    const size_t prefix = (!name.empty() && name[0] == '/') ? 1 : 0;
    // The start of the last component: the first one after the leading '/'s,
    // then each one that follows a '/'.
    size_t i = prefix;
    while (i < name.size() && name[i] == '/') {
        ++i;
    }
    size_t last = i;
    while (i < name.size()) {
        if (name[i] == '/' && i + 1 < name.size() && name[i + 1] != '/') {
            last = i + 1;
        }
        ++i;
    }
    size_t length = last;
    while (length > prefix && name[length - 1] == '/') {
        --length;
    }
    if (length == 0) {
        return ".";
    }
    return name.substr(0, length);
}

class DirnameCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "dirname"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'z', "zero", kOptionZero, BuiltinArgument::None, "", "end each output line with NUL, not newline"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "strip last component from file name",
            {"dirname [OPTION] NAME..."},
            ""};
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }
        bool zero = false;
        for (const auto& option : parsed->options) {
            if (option.id == kOptionZero) {
                zero = true;
            }
        }
        if (parsed->operands.empty()) {
            context.Error("missing operand");
            context.TryHelp();
            return 1;
        }
        const char end = zero ? '\0' : '\n';
        for (const auto& name : parsed->operands) {
            context.Out(DirName(name) + end);
        }
        return 0;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateDirnameCommand() {
    return std::make_shared<DirnameCommand>();
}

} // namespace Haisos