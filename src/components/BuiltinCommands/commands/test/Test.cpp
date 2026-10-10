#include <memory>
#include <string>
#include <vector>

#include "BuiltinCommand.h"
#include "BuiltinTestExpression.h"

namespace Haisos {

namespace {

// test and [ are one program with two names, GNU coreutils' test: an
// expression of file tests, string and integer comparisons joined by
// -a/-o, evaluated by BuiltinTestExpression's Gnu dialect. It takes no
// options -- every argument is part of the expression -- except that [,
// as GNU's, honors --help and --version when they are its sole argument;
// test treats those as any other nonempty string.
class TestCommand : public IBuiltinCommand {
public:
    explicit TestCommand(bool bracket) : m_bracket(bracket) {}

    std::string Name() const override { return m_bracket ? "[" : "test"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options;
        return options;
    }

    BuiltinHelp Help() const override {
        // GNU's test and [ print one usage for both names, test's first;
        // here each name's own forms come first, so each help is its own.
        std::vector<std::string> usage;
        if (m_bracket) {
            usage = {"[ EXPRESSION ]", "[ ]", "[ OPTION", "test EXPRESSION", "test"};
        } else {
            usage = {"test EXPRESSION", "test", "[ EXPRESSION ]", "[ ]", "[ OPTION"};
        }
        return BuiltinHelp{
            "check file types and compare values",
            usage,
            "An omitted EXPRESSION is false. It is built from:\n"
            "  ( EXPR )   ! EXPR   EXPR -a EXPR   EXPR -o EXPR\n"
            "  -n STRING, STRING: its length nonzero; -z STRING: zero;\n"
            "  S1 = S2, S1 != S2; -l STRING is STRING's length, an integer\n"
            "  I1 -eq -ne -lt -le -gt -ge I2: integers, any length;\n"
            "  FILE1 -ef FILE2: the same file; -nt, -ot: newer, older;\n"
            "  -b -c -d -e -f -g -G -h -k -L -N -O -p -r -s -S -u -w -x FILE;\n"
            "  -t FD: the descriptor is a terminal.\n"
            "[ with --help or --version as its sole argument shows this help\n"
            "or the version; test treats each of those as any other nonempty\n"
            "string, as GNU's does.\n"
            "There are no permissions or users: -r, -w, -x, -O and -G only\n"
            "test that the file is there. There are no links, block devices,\n"
            "fifos, sockets or set-id bits: -h, -L, -b, -p, -S, -u, -g and -k\n"
            "never match. -ef compares the paths as realpath resolves them\n"
            "(there are no inode numbers).",
            m_bracket ? "test" : "",
        };
    }

    int Run(BuiltinContext& context) override {
        const auto& args = context.Args();
        if (!m_bracket) {
            if (args.empty()) {
                return 1;  // no expression: false
            }
            return Evaluate(context, 0, args.size());
        }
        if (args.size() == 1 && (args[0] == "--help" || args[0] == "--version")) {
            context.Out(args[0] == "--help" ? BuiltinHelpText(*this) : BuiltinVersionText(*this));
            return 0;
        }
        if (args.empty() || args.back() != "]") {
            context.Error("missing ']'");
            return 2;
        }
        const size_t end = args.size() - 1;  // "]" is never an operand
        if (end == 0) {
            return 1;  // no expression: false
        }
        return Evaluate(context, 0, end);
    }

private:
    int Evaluate(BuiltinContext& context, size_t begin, size_t end) {
        return EvaluateTestExpression(TestDialect::Gnu, Name(), context.Args(), begin, end,
            context.IO(), [&context](const std::string& message) { context.Error(message); });
    }

    const bool m_bracket;
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateTestCommand() {
    return std::make_shared<TestCommand>(false);
}

std::shared_ptr<IBuiltinCommand> CreateBracketCommand() {
    return std::make_shared<TestCommand>(true);
}

} // namespace Haisos