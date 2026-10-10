#include "BuiltinCommand.h"
#include "BuiltinText.h"
#include <string>
#include <vector>

namespace Haisos {

namespace {

constexpr int kOptionMultiple = 1;
constexpr int kOptionSuffix = 2;
constexpr int kOptionZero = 3;

// NAME with any leading directory components removed, then SUFFIX when the
// base is longer than it and ends with it: an empty name stays empty, a
// string of only '/' is '/', otherwise the part after the last '/' with the
// trailing '/'s stripped.
std::string BaseName(const std::string& name, const std::string& suffix) {
    if (name.empty()) {
        return "";
    }
    bool allSlashes = true;
    for (const char c : name) {
        if (c != '/') {
            allSlashes = false;
            break;
        }
    }
    if (allSlashes) {
        return "/";
    }
    size_t end = name.size();
    while (end > 0 && name[end - 1] == '/') {
        --end;
    }
    size_t start = name.rfind('/', end - 1);
    start = (start == std::string::npos) ? 0 : start + 1;
    std::string base = name.substr(start, end - start);
    if (!suffix.empty() && base.size() > suffix.size() &&
        base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0) {
        base.resize(base.size() - suffix.size());
    }
    return base;
}

class BasenameCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "basename"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'a', "multiple", kOptionMultiple, BuiltinArgument::None, "", "support multiple arguments and treat each as a NAME"},
            {'s', "suffix", kOptionSuffix, BuiltinArgument::Required, "SUFFIX", "remove a trailing SUFFIX; implies -a"},
            {'z', "zero", kOptionZero, BuiltinArgument::None, "", "end each output line with NUL, not newline"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "strip directory and suffix from filenames",
            {"basename NAME [SUFFIX]", "basename OPTION... NAME..."},
            ""};
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }
        bool multiple = false;
        bool zero = false;
        std::string suffix;
        for (const auto& option : parsed->options) {
            switch (option.id) {
                case kOptionMultiple: multiple = true; break;
                case kOptionSuffix: multiple = true; suffix = option.argument; break;
                case kOptionZero: zero = true; break;
                default: break;  // not treated (already reported)
            }
        }
        if (parsed->operands.empty()) {
            context.Error("missing operand");
            context.TryHelp();
            return 1;
        }
        if (!multiple) {
            if (parsed->operands.size() >= 3) {
                context.Error("extra operand " + GnuQuote(parsed->operands[2]));
                context.TryHelp();
                return 1;
            }
            if (parsed->operands.size() == 2) {
                suffix = parsed->operands[1];
            }
        }
        const char end = zero ? '\0' : '\n';
        if (multiple) {
            for (const auto& name : parsed->operands) {
                context.Out(BaseName(name, suffix) + end);
            }
        } else {
            context.Out(BaseName(parsed->operands.front(), suffix) + end);
        }
        return 0;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateBasenameCommand() {
    return std::make_shared<BasenameCommand>();
}

} // namespace Haisos