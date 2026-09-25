#include <cstdio>
#include "BuiltinCommand.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {

namespace {

enum CatOption {
    kShowAll = 1,
    kNumberNonblank,
    kShowEndsAndNonprinting,
    kShowEnds,
    kNumber,
    kSqueezeBlank,
    kShowTabsAndNonprinting,
    kShowTabs,
    kIgnored,
    kShowNonprinting,
};

struct CatSettings {
    bool numberAll = false;
    bool numberNonblank = false;
    bool squeezeBlank = false;
    bool showEnds = false;
    bool showTabs = false;
    bool showNonprinting = false;
};

// Formats content the way cat would, carrying the line count and the
// blank-line run over from one file to the next, as cat does.
class CatFormatter {
public:
    explicit CatFormatter(const CatSettings& settings) : m_settings(settings) {}

    void Append(const char* data, size_t size, std::string& out) {
        for (size_t i = 0; i < size; ++i) {
            const unsigned char c = static_cast<unsigned char>(data[i]);
            if (m_atLineStart) {
                if (c == '\n') {
                    ++m_blankRun;
                    if (m_settings.squeezeBlank && m_blankRun > 1) {
                        continue;
                    }
                    if (m_settings.numberAll && !m_settings.numberNonblank) {
                        AppendLineNumber(out);
                    }
                    if (m_settings.showEnds) {
                        out += '$';
                    }
                    out += '\n';
                    continue;
                }
                m_blankRun = 0;
                if (m_settings.numberAll || m_settings.numberNonblank) {
                    AppendLineNumber(out);
                }
                m_atLineStart = false;
            }
            if (c == '\n') {
                if (m_settings.showEnds) {
                    out += '$';
                }
                out += '\n';
                m_atLineStart = true;
            } else if (c == '\t') {
                out += m_settings.showTabs ? "^I" : "\t";
            } else if (m_settings.showNonprinting) {
                AppendVisible(c, out);
            } else {
                out += static_cast<char>(c);
            }
        }
    }

private:
    void AppendLineNumber(std::string& out) {
        char number[32];
        std::snprintf(number, sizeof(number), "%6llu\t", ++m_lineNumber);
        out += number;
    }

    static void AppendVisible(unsigned char c, std::string& out) {
        if (c >= 128) {
            out += "M-";
            c = static_cast<unsigned char>(c - 128);
        }
        if (c < 32) {
            out += '^';
            out += static_cast<char>(c + 64);
        } else if (c == 127) {
            out += "^?";
        } else {
            out += static_cast<char>(c);
        }
    }

    const CatSettings& m_settings;
    bool m_atLineStart = true;
    unsigned long long m_lineNumber = 0;
    int m_blankRun = 0;
};

class CatCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "cat"; }
    std::string Version() const override { return "1.1.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'A', "show-all", kShowAll, BuiltinArgument::None, "", "same as -vET"},
            {'b', "number-nonblank", kNumberNonblank, BuiltinArgument::None, "", "number nonempty lines"},
            {'e', "", kShowEndsAndNonprinting, BuiltinArgument::None, "", "same as -vE"},
            {'E', "show-ends", kShowEnds, BuiltinArgument::None, "", "$ at end of each line"},
            {'n', "number", kNumber, BuiltinArgument::None, "", "number all lines"},
            {'s', "squeeze-blank", kSqueezeBlank, BuiltinArgument::None, "", "squeeze repeated empty lines"},
            {'t', "", kShowTabsAndNonprinting, BuiltinArgument::None, "", "same as -vT"},
            {'T', "show-tabs", kShowTabs, BuiltinArgument::None, "", "TAB as ^I"},
            {'u', "", kIgnored, BuiltinArgument::None, "", "(ignored, as in GNU cat)"},
            {'v', "show-nonprinting", kShowNonprinting, BuiltinArgument::None, "", "^ and M- notation"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "concatenate files and print on the standard output",
            {"cat [OPTION]... [FILE]..."},
            "No standard input: at least one FILE, and not -.\n"};
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }

        CatSettings settings;
        for (const auto& option : parsed->options) {
            switch (option.id) {
                case kShowAll: settings.showNonprinting = settings.showEnds = settings.showTabs = true; break;
                case kNumberNonblank: settings.numberNonblank = true; break;
                case kShowEndsAndNonprinting: settings.showNonprinting = settings.showEnds = true; break;
                case kShowEnds: settings.showEnds = true; break;
                case kNumber: settings.numberAll = true; break;
                case kSqueezeBlank: settings.squeezeBlank = true; break;
                case kShowTabsAndNonprinting: settings.showNonprinting = settings.showTabs = true; break;
                case kShowTabs: settings.showTabs = true; break;
                case kShowNonprinting: settings.showNonprinting = true; break;
                default: break;
            }
        }

        if (parsed->operands.empty()) {
            context.Error("reading standard input is not supported: HaisosOS has no stdin yet");
            context.TryHelp();
            return 1;
        }

        CatFormatter formatter(settings);
        int status = 0;
        for (const auto& file : parsed->operands) {
            if (context.StopRequested()) {
                return 1;
            }
            if (file == "-") {
                context.Error("-: reading standard input is not supported: HaisosOS has no stdin yet");
                status = 1;
                continue;
            }
            if (!CatFile(context, file, formatter)) {
                status = 1;
            }
        }
        return status;
    }

private:
    static bool CatFile(BuiltinContext& context, const std::string& file, CatFormatter& formatter) {
        IFileIO& io = context.IO();
        auto type = EntryTypeOf(io, io.ResolvePath(file));
        if (!type) {
            context.Error(file + ": No such file or directory");
            return false;
        }
        if (*type == DirectoryEntryType::Dir) {
            context.Error(file + ": Is a directory");
            return false;
        }
        const int fd = io.OpenFile(file, kFileOpenReadOnly);
        if (fd < 0) {
            context.Error(file + ": Permission denied");
            return false;
        }
        char buffer[64 * 1024];
        bool ok = true;
        while (true) {
            if (context.StopRequested()) {
                ok = false;
                break;
            }
            const ssize_t n = io.ReadFile(fd, buffer, sizeof(buffer));
            if (n == 0) {
                break;
            }
            if (n < 0) {
                context.Error(file + ": Input/output error");
                ok = false;
                break;
            }
            std::string out;
            formatter.Append(buffer, static_cast<size_t>(n), out);
            context.Out(out);
        }
        io.CloseFile(fd);
        return ok;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateCatCommand() {
    return std::make_shared<CatCommand>();
}

} // namespace Haisos
