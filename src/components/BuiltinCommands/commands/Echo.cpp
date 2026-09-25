#include "BuiltinCommand.h"

namespace Haisos {

namespace {

constexpr int kOptionNoNewline = 1;
constexpr int kOptionEscapes = 2;
constexpr int kOptionNoEscapes = 3;

int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Interprets the -e escapes in text, appending to out. Returns false once \c
// is met: nothing after it, not even the trailing newline, is output.
bool AppendWithEscapes(const std::string& text, std::string& out) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 >= text.size()) {
            out += text[i];
            continue;
        }
        const char code = text[++i];
        switch (code) {
            case '\\': out += '\\'; break;
            case 'a': out += '\a'; break;
            case 'b': out += '\b'; break;
            case 'c': return false;
            case 'e': out += '\x1b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'v': out += '\v'; break;
            case '0': {
                int value = 0;
                for (int digits = 0; digits < 3 && i + 1 < text.size() && text[i + 1] >= '0' && text[i + 1] <= '7'; ++digits) {
                    value = value * 8 + (text[++i] - '0');
                }
                out += static_cast<char>(value);
                break;
            }
            case 'x': {
                if (i + 1 >= text.size() || HexValue(text[i + 1]) < 0) {
                    // Not an escape after all: kept as written.
                    out += "\\x";
                    break;
                }
                int value = 0;
                for (int digits = 0; digits < 2 && i + 1 < text.size() && HexValue(text[i + 1]) >= 0; ++digits) {
                    value = value * 16 + HexValue(text[++i]);
                }
                out += static_cast<char>(value);
                break;
            }
            default:
                // An unknown escape is output as written, backslash included.
                out += '\\';
                out += code;
                break;
        }
    }
    return true;
}

class EchoCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "echo"; }
    std::string Version() const override { return "1.1.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'n', "", kOptionNoNewline, BuiltinArgument::None, "", "no trailing newline"},
            {'e', "", kOptionEscapes, BuiltinArgument::None, "", "interpret backslash escapes"},
            {'E', "", kOptionNoEscapes, BuiltinArgument::None, "", "do not interpret them (default)"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "display a line of text",
            {"echo [SHORT-OPTION]... [STRING]...", "echo LONG-OPTION"},
            "With -e: \\\\ \\a \\b \\c (stop) \\e \\f \\n \\r \\t \\v \\0NNN \\xHH.\n"
            "--help and --version count only as the sole argument.\n"};
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

        // Not getopt: GNU echo takes options only from the leading words made
        // entirely of n, e and E after a '-'; the first word that is anything
        // else starts the text, even if it looks like an option.
        bool trailingNewline = true;
        bool escapes = false;
        size_t first = 0;
        for (; first < args.size(); ++first) {
            const std::string& arg = args[first];
            if (arg.size() < 2 || arg[0] != '-' || arg.find_first_not_of("neE", 1) != std::string::npos) {
                break;
            }
            for (size_t j = 1; j < arg.size(); ++j) {
                if (arg[j] == 'n') trailingNewline = false;
                else if (arg[j] == 'e') escapes = true;
                else escapes = false;
            }
        }

        std::string out;
        for (size_t i = first; i < args.size(); ++i) {
            if (i > first) {
                out += ' ';
            }
            if (!escapes) {
                out += args[i];
            } else if (!AppendWithEscapes(args[i], out)) {
                context.Out(out);
                return 0;
            }
        }
        if (trailingNewline) {
            out += '\n';
        }
        context.Out(out);
        return 0;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateEchoCommand() {
    return std::make_shared<EchoCommand>();
}

} // namespace Haisos
