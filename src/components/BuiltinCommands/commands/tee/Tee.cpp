#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinText.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {

namespace {

enum TeeOption {
    kTeeAppend = 1,
    kTeePipeMode,
    kTeeOutputError,
};

// The --output-error MODEs; TeeDefault is no --output-error and no -p.
enum TeeMode {
    TeeDefault,
    TeeWarn,
    TeeWarnNopipe,
    TeeExit,
    TeeExitNopipe,
};

class TeeCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "tee"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'a', "append", kTeeAppend, BuiltinArgument::None, "",
                "append to the given FILEs, do not overwrite"},
            {'i', "ignore-interrupts", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {'p', "", kTeePipeMode, BuiltinArgument::None, "",
                "operate in a more appropriate MODE with pipes"},
            {0, "output-error", kTeeOutputError, BuiltinArgument::Optional, "MODE",
                "set behavior on write error: warn, warn-nopipe, exit, exit-nopipe"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "copy standard input to each FILE, and also to standard output",
            {"tee [OPTION]... [FILE]..."},
            "MODE, for --output-error: 'warn' diagnose write errors and continue,\n"
            "'warn-nopipe' the same but silent on broken pipes, 'exit' exit on the\n"
            "first write error, 'exit-nopipe' the same but silent on broken pipes.\n"
            "Without it, a write to a broken pipe stops tee as SIGPIPE does, and a\n"
            "write error on a file is diagnosed and gone past; -p and a bare\n"
            "--output-error are --output-error=warn-nopipe. tee stops reading once\n"
            "no output is left.",
        };
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }

        bool append = false;
        int mode = TeeDefault;
        for (const auto& option : parsed->options) {
            switch (option.id) {
                case kTeeAppend:
                    append = true;
                    break;
                case kTeePipeMode:
                    mode = TeeWarnNopipe;
                    break;
                case kTeeOutputError: {
                    if (!option.hasArgument) {
                        mode = TeeWarnNopipe;
                        break;
                    }
                    const auto matched = ArgMatch(context, "--output-error", option.argument,
                        {{"warn", TeeWarn},
                         {"warn-nopipe", TeeWarnNopipe},
                         {"exit", TeeExit},
                         {"exit-nopipe", TeeExitNopipe}});
                    if (!matched) {
                        return 1;
                    }
                    mode = *matched;
                    break;
                }
            }
        }

        int status = 0;
        IFileIO& io = context.IO();

        // Every FILE is opened up front, as GNU's does -- an unreadable one is
        // reported and skipped, the rest go on. "-" is a file's name, not the
        // standard input, as GNU takes it.
        std::vector<std::string> fileNames;
        std::vector<std::shared_ptr<IFileDescriptor>> files;
        for (const auto& operand : parsed->operands) {
            FileStatus fileStatus;
            const bool exists = io.Stat(operand, fileStatus) == 0;
            if (exists && fileStatus.type == DirectoryEntryType::Dir) {
                context.Error(ShellEscapeQuoted(operand) + ": Is a directory");
                status = 1;
                continue;
            }
            auto handle = io.OpenFile(operand,
                append ? kFileOpenWriteCreateAppend : kFileOpenWriteCreateTruncate,
                kFileCreateMode);
            if (!handle) {
                context.Error(ShellEscapeQuoted(operand) + ": "
                    + (exists ? "Permission denied" : "No such file or directory"));
                status = 1;
                continue;
            }
            fileNames.push_back(operand);
            files.push_back(std::move(handle));
        }

        const auto input = io.GetDescriptor(IFileIO::kStdIn);
        if (!input) {
            return status;
        }
        auto output = io.GetDescriptor(IFileIO::kStdOut);
        bool outputDead = false; // dropped after a write error; skipped from then on
        std::vector<bool> fileDead(files.size(), false);

        // What one failed write on one output means; the caller goes on from
        // it, or returns at once for kTeeExit / kTeeBrokenPipe.
        enum class WriteResult { Written, Dropped, Warned, Exit, BrokenPipe };
        auto writeOne = [&](IFileDescriptor& out, const std::string& name,
                            std::string_view bytes) {
            const ssize_t result = WriteFully(out, bytes);
            if (result >= 0) {
                return WriteResult::Written;
            }
            const bool brokenPipe = result == kIOBrokenPipe;
            if (brokenPipe && mode == TeeDefault) {
                // A Linux tee here dies of SIGPIPE, quietly, at once.
                context.Process().StopForBrokenPipe();
                return WriteResult::BrokenPipe;
            }
            const bool diagnose = mode == TeeDefault || mode == TeeWarn || mode == TeeExit
                || ((mode == TeeWarnNopipe || mode == TeeExitNopipe) && !brokenPipe);
            if (diagnose) {
                context.Error(ShellEscapeQuoted(name) + ": "
                    + std::string(brokenPipe ? "Broken pipe" : "Input/output error"));
            }
            if (mode == TeeExit || (mode == TeeExitNopipe && !brokenPipe)) {
                return WriteResult::Exit;
            }
            return diagnose ? WriteResult::Warned : WriteResult::Dropped;
        };

        char buffer[64 * 1024];
        while (true) {
            if (context.StopRequested()) {
                return status;
            }
            const ssize_t n = input->Read(buffer, sizeof(buffer));
            if (n == 0) {
                return status;
            }
            if (n == kIOInterrupted) {
                return status; // quiet: the process reports its own stop code
            }
            if (n < 0) {
                context.Error("read error: Input/output error");
                return 1;
            }
            const std::string_view chunk(buffer, static_cast<size_t>(n));
            if (output && !outputDead) {
                switch (writeOne(*output, "standard output", chunk)) {
                    case WriteResult::Written:
                        break;
                    case WriteResult::Warned:
                        status = 1;
                        outputDead = true;
                        break;
                    case WriteResult::Dropped:
                        outputDead = true;
                        break;
                    case WriteResult::Exit:
                        return 1;
                    case WriteResult::BrokenPipe:
                        return status;
                }
            }
            for (size_t i = 0; i < files.size(); ++i) {
                if (fileDead[i]) {
                    continue;
                }
                switch (writeOne(*files[i], fileNames[i], chunk)) {
                    case WriteResult::Written:
                        break;
                    case WriteResult::Warned:
                        status = 1;
                        fileDead[i] = true;
                        break;
                    case WriteResult::Dropped:
                        fileDead[i] = true;
                        break;
                    case WriteResult::Exit:
                        return 1;
                    case WriteResult::BrokenPipe:
                        return status;
                }
            }
            // In every MODE, tee stops once no output is left, as GNU's does --
            // nothing it reads could go anywhere.
            if (!output || outputDead) {
                bool anyAlive = false;
                for (size_t i = 0; i < files.size(); ++i) {
                    if (!fileDead[i]) {
                        anyAlive = true;
                    }
                }
                if (!anyAlive) {
                    return status;
                }
            }
        }
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateTeeCommand() {
    return std::make_shared<TeeCommand>();
}

} // namespace Haisos