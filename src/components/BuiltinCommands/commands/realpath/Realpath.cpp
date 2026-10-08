#include "BuiltinCommand.h"
#include <string>
#include <vector>
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {

namespace {

constexpr int kOptionExisting = 1;   // -e
constexpr int kOptionMissing = 2;    // -m
constexpr int kOptionLogical = 3;    // -L
constexpr int kOptionPhysical = 4;   // -P
constexpr int kOptionQuiet = 5;      // -q
constexpr int kOptionRelativeTo = 6;
constexpr int kOptionRelativeBase = 7;
constexpr int kOptionStrip = 8;      // -s, --strip, --no-symlinks
constexpr int kOptionZero = 9;       // -z

// How strictly a path is checked: -e wants everything there, -m nothing,
// the default everything but the last segment.
enum class ResolveMode { Default, Existing, Missing };

// GNU's quotef: a name in a message, quoted only when it needs quoting.
std::string qf(const std::string& name) {
    return ShellEscapeQuoted(name);
}

// The path the segments build: "/" when there are none.
std::string PathOf(const std::vector<std::string>& segments) {
    if (segments.empty()) {
        return "/";
    }
    std::string path;
    for (const auto& segment : segments) {
        path += "/" + segment;
    }
    return path;
}

// The segments an absolute path is made of.
std::vector<std::string> SegmentsOf(const std::string& path) {
    std::vector<std::string> segments;
    size_t i = 0;
    while (i < path.size()) {
        while (i < path.size() && path[i] == '/') {
            ++i;
        }
        if (i >= path.size()) {
            break;
        }
        size_t end = i;
        while (end < path.size() && path[end] != '/') {
            ++end;
        }
        segments.push_back(path.substr(i, end - i));
        i = end;
    }
    return segments;
}

// Whether |path| is |base| itself or under it.
bool IsAtOrUnder(const std::string& path, const std::string& base) {
    const auto p = SegmentsOf(path);
    const auto b = SegmentsOf(base);
    if (b.size() > p.size()) {
        return false;
    }
    for (size_t i = 0; i < b.size(); ++i) {
        if (p[i] != b[i]) {
            return false;
        }
    }
    return true;
}

// |path| as it is written from |dir| (both absolute): the common leading
// segments dropped, one ".." per segment the directory has left, then the
// segments the path has left; "." when they are the same.
std::string RelativeTo(const std::string& dir, const std::string& path) {
    const auto d = SegmentsOf(dir);
    const auto p = SegmentsOf(path);
    size_t common = 0;
    while (common < d.size() && common < p.size() && d[common] == p[common]) {
        ++common;
    }
    if (common == d.size() && common == p.size()) {
        return ".";
    }
    std::string result;
    for (size_t i = common; i < d.size(); ++i) {
        result += result.empty() ? ".." : "/..";
    }
    for (size_t i = common; i < p.size(); ++i) {
        if (!result.empty()) {
            result += "/";
        }
        result += p[i];
    }
    return result;
}

// Resolves one operand, from the process's working directory, as there are
// no symlinks: '' and '.' segments are skipped, '..' drops the last segment
// kept (at the root it stays), and outside -m every segment but the last
// must be an existing directory; with -e the last must exist too. A trailing
// '/' on the operand (outside -m) wants the result to be a directory: what
// is there and is not one is "Not a directory". False, with *error filled,
// when the path cannot be resolved.
bool ResolveOne(IFileIO& io, const std::string& operand, ResolveMode mode,
                std::string& resolved, std::string& error) {
    if (operand.empty()) {
        error = "No such file or directory";
        return false;
    }
    const std::string base = operand[0] == '/' ? operand : io.GetCurrentDirectory() + "/" + operand;
    std::vector<std::string> kept;
    size_t i = 0;
    while (i < base.size()) {
        while (i < base.size() && base[i] == '/') {
            ++i;
        }
        if (i >= base.size()) {
            break;
        }
        size_t end = i;
        while (end < base.size() && base[end] != '/') {
            ++end;
        }
        const std::string segment = base.substr(i, end - i);
        // Last: nothing but '/'s follow it -- so a trailing '/' does not
        // make what precedes it an intermediate segment (missing is fine
        // then; the trailing-'/' check below reports a non-directory).
        bool last = true;
        for (size_t j = end; j < base.size(); ++j) {
            if (base[j] != '/') {
                last = false;
                break;
            }
        }
        i = end;
        if (segment == ".") {
            continue;
        }
        if (segment == "..") {
            if (!kept.empty()) {
                kept.pop_back();
            }
            continue;
        }
        kept.push_back(segment);
        if (mode != ResolveMode::Missing) {
            const std::string soFar = PathOf(kept);
            if (!last) {
                const auto type = EntryTypeOf(io, soFar);
                if (!type) {
                    error = "No such file or directory";
                    return false;
                }
                if (*type != DirectoryEntryType::Dir) {
                    error = "Not a directory";
                    return false;
                }
            } else if (mode == ResolveMode::Existing && !EntryTypeOf(io, soFar)) {
                error = "No such file or directory";
                return false;
            }
        }
    }
    resolved = PathOf(kept);
    if (operand.back() == '/' && mode != ResolveMode::Missing) {
        const auto type = EntryTypeOf(io, resolved);
        if (type && *type != DirectoryEntryType::Dir) {
            error = "Not a directory";
            return false;
        }
    }
    return true;
}

// How one resolved path prints: absolute outside a base, relative to
// --relative-to (or to the base, when it alone is given).
std::string PrintedPath(const std::string& resolved, const std::string& relativeTo,
                        const std::string& relativeBase) {
    if (!relativeBase.empty() && !IsAtOrUnder(resolved, relativeBase)) {
        return resolved;
    }
    if (!relativeTo.empty()) {
        return RelativeTo(relativeTo, resolved);
    }
    if (!relativeBase.empty()) {
        return RelativeTo(relativeBase, resolved);
    }
    return resolved;
}

class RealpathCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "realpath"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'e', "canonicalize-existing", kOptionExisting, BuiltinArgument::None, "", "all components of the path must exist"},
            {'m', "canonicalize-missing", kOptionMissing, BuiltinArgument::None, "", "no path components need exist or be a directory"},
            {'L', "logical", kOptionLogical, BuiltinArgument::None, "", "resolve '..' components before symlinks"},
            {'P', "physical", kOptionPhysical, BuiltinArgument::None, "", "resolve symlinks as encountered (default)"},
            {'q', "quiet", kOptionQuiet, BuiltinArgument::None, "", "suppress most error messages"},
            {0, "relative-to", kOptionRelativeTo, BuiltinArgument::Required, "DIR", "print the resolved path relative to DIR"},
            {0, "relative-base", kOptionRelativeBase, BuiltinArgument::Required, "DIR", "print absolute paths unless paths below DIR"},
            {'s', "strip", kOptionStrip, BuiltinArgument::None, "", "don't expand symlinks"},
            {0, "no-symlinks", kOptionStrip, BuiltinArgument::None, "", "the same as --strip (no symlinks exist)"},
            {'z', "zero", kOptionZero, BuiltinArgument::None, "", "end each output line with NUL, not newline"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "print the resolved path",
            {"realpath [OPTION]... FILE..."},
            "There are no symbolic links, so -L, -P and -s change nothing."};
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }
        ResolveMode mode = ResolveMode::Default;  // -e and -m: the last given wins
        bool quiet = false;
        bool zero = false;
        std::string relativeTo;
        std::string relativeBase;
        for (const auto& option : parsed->options) {
            switch (option.id) {
                case kOptionExisting: mode = ResolveMode::Existing; break;
                case kOptionMissing: mode = ResolveMode::Missing; break;
                case kOptionQuiet: quiet = true; break;
                case kOptionZero: zero = true; break;
                case kOptionRelativeTo: relativeTo = option.argument; break;
                case kOptionRelativeBase: relativeBase = option.argument; break;
                default: break;  // -L, -P, -s: no links, the same
            }
        }
        if (parsed->operands.empty()) {
            context.Error("missing operand");
            context.TryHelp();
            return 1;
        }
        IFileIO& io = context.IO();
        // The relative directories resolve by the same rules and mode; an
        // error there ends the command at once, not even -q silencing it.
        std::string relativeToPath;
        std::string relativeBasePath;
        if (!relativeTo.empty() || !relativeBase.empty()) {
            std::string error;
            if (!relativeTo.empty() && !ResolveOne(io, relativeTo, mode, relativeToPath, error)) {
                context.Error(qf(relativeTo) + ": " + error);
                return 1;
            }
            if (!relativeBase.empty() && !ResolveOne(io, relativeBase, mode, relativeBasePath, error)) {
                context.Error(qf(relativeBase) + ": " + error);
                return 1;
            }
            // --relative-to outside --relative-base: both are dropped.
            if (!relativeToPath.empty() && !relativeBasePath.empty() &&
                !IsAtOrUnder(relativeToPath, relativeBasePath)) {
                relativeToPath.clear();
                relativeBasePath.clear();
            }
        }
        int status = 0;
        const char end = zero ? '\0' : '\n';
        for (const auto& operand : parsed->operands) {
            std::string resolved;
            std::string error;
            if (!ResolveOne(io, operand, mode, resolved, error)) {
                if (!quiet) {
                    context.Error(qf(operand) + ": " + error);
                }
                status = 1;
                continue;
            }
            context.Out(PrintedPath(resolved, relativeToPath, relativeBasePath) + end);
        }
        return status;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateRealpathCommand() {
    return std::make_shared<RealpathCommand>();
}

} // namespace Haisos