#include "BuiltinCommand.h"
#include "commands/awk/AwkInvocation.h"
#include <memory>
#include <string>
#include <vector>

namespace Haisos {

namespace {

class AwkCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "awk"; }
    std::string Version() const override { return "1.0.1"; }

    const std::vector<BuiltinOption>& Options() const override {
        return Awk::AwkOptionTable();
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "pattern scanning and processing language",
            {"awk [-F fs] [-v var=value] [--] 'program' [file ...]",
             "awk [-F fs] [-v var=value] -f progfile [-f progfile]... [--] [file ...]"},
            "POSIX awk, as gawk --posix runs it.\n"
            "Usage errors print gawk's two usage lines; the option summary is\n"
            "`awk --help', not gawk's own.\n"
            "An `@' is reported as invalid char in expression where gawk reports a\n"
            "syntax error at the next token.\n"
            "A backslash-newline inside a string is a continuation; gawk --posix\n"
            "refuses a physical newline in a string.\n"
            "Running programs is not implemented yet.",
            "gawk",
        };
    }

    int Run(BuiltinContext& context) override {
        int status = 0;
        const std::optional<Awk::AwkInvocation> invocation =
            Awk::ParseAwkInvocation(context, *this, status);
        if (!invocation) {
            return status;
        }
        const std::optional<std::vector<Awk::AwkSource>> sources =
            Awk::LoadAwkSources(context, *invocation, status);
        if (!sources) {
            return status;
        }
        context.ErrorText("awk: running programs is not implemented yet\n");
        return 2;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateAwkCommand() {
    return std::make_shared<AwkCommand>();
}

} // namespace Haisos