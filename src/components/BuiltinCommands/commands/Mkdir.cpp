#include "BuiltinCommand.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {

namespace {

#ifdef _WIN32
constexpr int kDirectoryMode = _S_IREAD | _S_IWRITE;
#else
// What the real mkdir asks for; the host's umask trims it as usual.
constexpr int kDirectoryMode = S_IRWXU | S_IRWXG | S_IRWXO;
#endif

constexpr int kOptionParents = 1;
constexpr int kOptionVerbose = 2;

class MkdirCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "mkdir"; }
    std::string Version() const override { return "1.1.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'m', "mode", kBuiltinNotTreated, BuiltinArgument::Required, "MODE"},
            {'p', "parents", kOptionParents, BuiltinArgument::None, "", "make parents, no error if existing"},
            {'v', "verbose", kOptionVerbose, BuiltinArgument::None, "", "print each directory created"},
            {'Z', "", kBuiltinNotTreated},
            {0, "context", kBuiltinNotTreated, BuiltinArgument::Optional, "CTX"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{"make directories", {"mkdir [OPTION]... DIRECTORY..."}, ""};
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }
        bool parents = false;
        bool verbose = false;
        for (const auto& option : parsed->options) {
            if (option.id == kOptionParents) parents = true;
            if (option.id == kOptionVerbose) verbose = true;
        }
        if (parsed->operands.empty()) {
            context.Error("missing operand");
            context.TryHelp();
            return 1;
        }

        int status = 0;
        for (const auto& directory : parsed->operands) {
            if (context.StopRequested()) {
                return 1;
            }
            const bool ok = parents
                ? MakeWithParents(context, directory, verbose)
                : MakeOne(context, directory, directory, verbose, /*existingIsError=*/true);
            if (!ok) {
                status = 1;
            }
        }
        return status;
    }

private:
    // Creates |shown| (as the user wrote it; |path| is what to resolve). An
    // existing directory is an error unless -p made it a step on the way.
    static bool MakeOne(BuiltinContext& context, const std::string& path, const std::string& shown, bool verbose, bool existingIsError) {
        IFileIO& io = context.IO();
        const std::string absolute = io.ResolvePath(path);
        if (auto type = EntryTypeOf(io, absolute)) {
            if (*type == DirectoryEntryType::Dir && !existingIsError) {
                return true;
            }
            context.Error("cannot create directory '" + shown + "': File exists");
            return false;
        }
        const std::string parent = VirtualParentOf(absolute);
        auto parentType = EntryTypeOf(io, parent);
        if (!parentType) {
            context.Error("cannot create directory '" + shown + "': No such file or directory");
            return false;
        }
        if (*parentType != DirectoryEntryType::Dir) {
            context.Error("cannot create directory '" + shown + "': Not a directory");
            return false;
        }
        if (io.CreateDirectory(path, kDirectoryMode) != 0) {
            context.Error("cannot create directory '" + shown + "': Permission denied");
            return false;
        }
        if (verbose) {
            context.Out("mkdir: created directory '" + shown + "'\n");
        }
        return true;
    }

    // mkdir -p: every missing directory along the way, each shown as the
    // prefix of the operand that names it ("a", then "a/b").
    static bool MakeWithParents(BuiltinContext& context, const std::string& directory, bool verbose) {
        std::string prefix = (!directory.empty() && directory[0] == '/') ? "/" : "";
        size_t start = prefix.size();
        while (start <= directory.size()) {
            size_t slash = directory.find('/', start);
            const size_t end = (slash == std::string::npos) ? directory.size() : slash;
            const std::string segment = directory.substr(start, end - start);
            if (!segment.empty()) {
                if (!prefix.empty() && prefix.back() != '/') {
                    prefix += '/';
                }
                prefix += segment;
                if (segment != "." && segment != ".." &&
                    !MakeOne(context, prefix, prefix, verbose, /*existingIsError=*/false)) {
                    return false;
                }
            }
            if (slash == std::string::npos) {
                break;
            }
            start = slash + 1;
        }
        return true;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateMkdirCommand() {
    return std::make_shared<MkdirCommand>();
}

} // namespace Haisos
