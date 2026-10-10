#include "BuiltinCommand.h"
#include "BuiltinText.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {

namespace {

constexpr int kOptionChanges = 1;
constexpr int kOptionForce = 2;
constexpr int kOptionVerbose = 3;
constexpr int kOptionNoPreserveRoot = 4;
constexpr int kOptionPreserveRoot = 5;
constexpr int kOptionReference = 6;
constexpr int kOptionRecursive = 7;

// Every file's mode, since Haisos has no permissions: what ls -l shows as
// rwxrwxrwx.
constexpr int kEveryoneMode = 0777;

constexpr int kModeSetUid = 04000;
constexpr int kModeSetGid = 02000;
constexpr int kModeSticky = 01000;

constexpr int kWhoUser = 1;
constexpr int kWhoGroup = 2;
constexpr int kWhoOther = 4;

// GNU's quoteaf: every name in a chmod message, quoted even when plain.
std::string q(const std::string& name) {
    return ShellEscapeQuoted(name, /*always=*/true);
}

// The mode as the -v and -c lines print it: "0755 (rwxr-xr-x)", the nine
// characters with s/S, s/S and t/T where a special bit sits in an execute
// position.
std::string ModeText(int mode) {
    mode &= 07777;
    char digits[8];
    std::snprintf(digits, sizeof digits, "%04o", static_cast<unsigned>(mode));
    std::string bits;
    const int shifts[3] = {6, 3, 0};
    const int specials[3] = {kModeSetUid, kModeSetGid, kModeSticky};
    const char withExecute[3] = {'s', 's', 't'};
    const char withoutExecute[3] = {'S', 'S', 'T'};
    for (int i = 0; i < 3; ++i) {
        const int classBits = (mode >> shifts[i]) & 7;
        bits += (classBits & 4) ? 'r' : '-';
        bits += (classBits & 2) ? 'w' : '-';
        if (classBits & 1) {
            bits += (mode & specials[i]) ? withExecute[i] : 'x';
        } else {
            bits += (mode & specials[i]) ? withoutExecute[i] : '-';
        }
    }
    return std::string(digits) + " (" + bits + ")";
}

// Applies one symbolic mode to |mode| exactly as GNU 9.4 parses one, with
// umask 0: comma-separated clauses of a who ([ugoa]*, "all" when empty) and
// one or more [+-=] actions; an action's permissions are any [rwxXst] set
// (empty allowed: "u=" clears), or one of u/g/o (that class's bits of the
// mode as it stands), or -- only with no who given -- octal digits that end
// the clause ("=644"). X is x for a directory or a mode holding any x bit;
// s is setuid for u and setgid for g; t is the sticky bit for o. '+' adds,
// '-' removes, '=' sets the classes' bits to exactly these and clears their
// special bits first. Returns false on a mode GNU rejects.
bool ApplySymbolicMode(const std::string& spec, int& mode, bool isDirectory) {
    size_t i = 0;
    while (true) {
        int who = 0;
        bool whoGiven = false;
        while (i < spec.size() && spec[i] != '+' && spec[i] != '-' && spec[i] != '=' && spec[i] != ',') {
            whoGiven = true;
            if (spec[i] == 'u') {
                who |= kWhoUser;
            } else if (spec[i] == 'g') {
                who |= kWhoGroup;
            } else if (spec[i] == 'o') {
                who |= kWhoOther;
            } else if (spec[i] == 'a') {
                who |= kWhoUser | kWhoGroup | kWhoOther;
            } else {
                return false;
            }
            ++i;
        }
        if (!whoGiven) {
            who = kWhoUser | kWhoGroup | kWhoOther;
        }
        bool sawAction = false;
        while (i < spec.size() && (spec[i] == '+' || spec[i] == '-' || spec[i] == '=')) {
            const char op = spec[i++];
            // Octal digits, only with no who given: the whole mode, and the
            // clause ends there.
            if (i < spec.size() && spec[i] >= '0' && spec[i] <= '7') {
                if (whoGiven) {
                    return false;
                }
                int value = 0;
                while (i < spec.size() && spec[i] >= '0' && spec[i] <= '7') {
                    value = value * 8 + (spec[i] - '0');
                    if (value > 07777) {
                        return false;
                    }
                    ++i;
                }
                if (i < spec.size() && spec[i] != ',') {
                    return false;
                }
                if (op == '+') {
                    mode |= value;
                } else if (op == '-') {
                    mode &= ~value;
                } else {
                    mode = value;
                }
                sawAction = true;
                break;
            }
            int permBits = 0;
            int specialBits = 0;
            bool executeWhenWarranted = false;
            bool anyLetter = false;
            while (i < spec.size()) {
                const char c = spec[i];
                if (c == 'r') {
                    permBits |= 4;
                } else if (c == 'w') {
                    permBits |= 2;
                } else if (c == 'x') {
                    permBits |= 1;
                } else if (c == 'X') {
                    executeWhenWarranted = true;
                } else if (c == 's') {
                    if (who & kWhoUser) {
                        specialBits |= kModeSetUid;
                    }
                    if (who & kWhoGroup) {
                        specialBits |= kModeSetGid;
                    }
                } else if (c == 't') {
                    if (who & kWhoOther) {
                        specialBits |= kModeSticky;
                    }
                } else {
                    break;
                }
                anyLetter = true;
                ++i;
            }
            // One of u/g/o, only when no letter came before it: that class's
            // bits of the mode as it stands.
            if (i < spec.size() && (spec[i] == 'u' || spec[i] == 'g' || spec[i] == 'o')) {
                if (anyLetter) {
                    return false;
                }
                permBits = (mode >> (spec[i] == 'u' ? 6 : spec[i] == 'g' ? 3 : 0)) & 7;
                ++i;
            }
            if (executeWhenWarranted && (isDirectory || (mode & 0111) != 0)) {
                permBits |= 1;
            }
            int value = specialBits;
            if (who & kWhoUser) {
                value |= permBits << 6;
            }
            if (who & kWhoGroup) {
                value |= permBits << 3;
            }
            if (who & kWhoOther) {
                value |= permBits;
            }
            if (op == '+') {
                mode |= value;
            } else if (op == '-') {
                mode &= ~value;
            } else {
                // '=': the classes' bits set to exactly these, their special
                // bits cleared first (then the s/t of the value put back).
                int clear = 0;
                if (who & kWhoUser) {
                    clear |= 0700 | kModeSetUid;
                }
                if (who & kWhoGroup) {
                    clear |= 0070 | kModeSetGid;
                }
                if (who & kWhoOther) {
                    clear |= 0007 | kModeSticky;
                }
                mode &= ~clear;
                mode |= value;
            }
            sawAction = true;
        }
        if (!sawAction) {
            return false;
        }
        if (i == spec.size()) {
            return true;
        }
        if (spec[i] != ',') {
            return false;
        }
        ++i;
        if (i == spec.size()) {
            return false;  // a clause must follow the comma
        }
    }
}

// MODE as one operand gives it: octal (the whole mode, at most 07777) or
// symbolic, applied to the file's mode |oldMode|. |isDirectory| is what X
// goes by. Returns false on a mode GNU rejects; *outMode is then garbage.
bool ParseChmodMode(const std::string& spec, int oldMode, bool isDirectory, int& outMode) {
    bool octal = !spec.empty();
    for (const char c : spec) {
        if (c < '0' || c > '7') {
            octal = false;
            break;
        }
    }
    if (octal) {
        int value = 0;
        for (const char c : spec) {
            value = value * 8 + (c - '0');
            if (value > 07777) {
                return false;
            }
        }
        outMode = value;
        return true;
    }
    outMode = oldMode;
    return ApplySymbolicMode(spec, outMode, isDirectory);
}

// The mode options that apply to every file alike.
struct ChmodOptions {
    bool force = false;          // -f
    int verbosity = 0;           // 1: -c, 2: -v; the last given wins
    bool recursive = false;      // -R
    bool preserveRoot = false;   // --preserve-root (GNU 9.4: off by default)
    bool fromReference = false;  // --reference
    std::string modeSpec;
};

// Chmods one operand (and with -R its entries after it, in name order):
// finds it, computes its new mode, reports the change -- and changes nothing,
// since Haisos has no permissions.
bool ChmodFile(BuiltinContext& context, const std::string& path, const ChmodOptions& options) {
    if (context.StopRequested()) {
        return false;
    }
    IFileIO& io = context.IO();
    FileStatus fileStatus;
    if (io.Stat(path, fileStatus) != 0) {
        if (!options.force) {
            context.Error("cannot access " + q(path) + ": No such file or directory");
        }
        if (options.verbosity == 2) {
            context.Out(q(path) + " could not be accessed\n");
        }
        return false;
    }
    const bool isDirectory = fileStatus.type == DirectoryEntryType::Dir;
    if (options.recursive && isDirectory && options.preserveRoot && io.ResolvePath(path) == "/") {
        context.Error("it is dangerous to operate recursively on " + q(path) +
            (path == "/" ? "" : " (same as '/')"));
        context.Error("use --no-preserve-root to override this failsafe");
        return false;
    }
    int newMode = kEveryoneMode;
    if (!options.fromReference && !ParseChmodMode(options.modeSpec, kEveryoneMode, isDirectory, newMode)) {
        return false;  // cannot happen: the mode was validated up front
    }
    if (options.verbosity == 2 || (options.verbosity == 1 && newMode != kEveryoneMode)) {
        if (newMode == kEveryoneMode) {
            context.Out("mode of " + q(path) + " retained as " + ModeText(kEveryoneMode) + "\n");
        } else {
            context.Out("mode of " + q(path) + " changed from " + ModeText(kEveryoneMode) +
                " to " + ModeText(newMode) + "\n");
        }
    }
    if (options.recursive && isDirectory) {
        std::vector<std::string> names;
        for (const auto& entry : io.ReadDirectory(path)) {
            if (entry.name != "." && entry.name != "..") {
                names.push_back(entry.name);
            }
        }
        std::sort(names.begin(), names.end());
        std::string base = path;
        while (base.size() > 1 && base.back() == '/') {
            base.pop_back();
        }
        bool allOk = true;
        for (const auto& name : names) {
            allOk = ChmodFile(context, base + (base == "/" ? "" : "/") + name, options) && allOk;
        }
        return allOk;
    }
    return true;
}

class ChmodCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "chmod"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'c', "changes", kOptionChanges, BuiltinArgument::None, "", "like verbose but report only when a change is made"},
            {'f', "silent", kOptionForce, BuiltinArgument::None, "", "suppress most error messages"},
            {0, "quiet", kOptionForce, BuiltinArgument::None, "", "the same as --silent"},
            {'v', "verbose", kOptionVerbose, BuiltinArgument::None, "", "output a diagnostic for every file processed"},
            {0, "no-preserve-root", kOptionNoPreserveRoot, BuiltinArgument::None, "", "do not treat '/' specially (the default)"},
            {0, "preserve-root", kOptionPreserveRoot, BuiltinArgument::None, "", "fail to operate recursively on '/'"},
            {0, "reference", kOptionReference, BuiltinArgument::Required, "RFILE", "use RFILE's mode instead of MODE values"},
            {'R', "recursive", kOptionRecursive, BuiltinArgument::None, "", "change files and directories recursively"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "change file mode bits",
            {"chmod [OPTION]... MODE[,MODE]... FILE...",
             "chmod [OPTION]... OCTAL-MODE FILE...",
             "chmod [OPTION]... --reference=RFILE FILE..."},
            "Haisos has no permissions: the mode is parsed and validated, the files\n"
            "must exist, and nothing changes. Every file's mode is taken as 0777\n"
            "(as ls -l shows rwxrwxrwx), and the umask as 0."};
    }

    int Run(BuiltinContext& context) override {
        // A mode may look like an option ("chmod -w f"): every argument
        // before a "--" that starts with '-' and holds only mode bytes is the
        // mode (joined with ',' when there are several), out of the arguments
        // before they are parsed, so no option parser ever sees it.
        std::vector<std::string> args = context.Args();
        std::string scannedMode;
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--") {
                break;
            }
            const std::string& arg = args[i];
            if (arg.size() < 2 || arg[0] != '-') {
                continue;
            }
            bool modeWord = true;
            for (size_t j = 1; j < arg.size(); ++j) {
                if (std::string_view("rwxXstugoa,+-=01234567").find(arg[j]) == std::string_view::npos) {
                    modeWord = false;
                    break;
                }
            }
            if (modeWord) {
                scannedMode += scannedMode.empty() ? arg : "," + arg;
                args.erase(args.begin() + i);
                --i;
            }
        }

        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, args, *this, /*usageErrorStatus=*/1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }

        ChmodOptions options;
        std::string referenceFile;
        for (const auto& option : parsed->options) {
            switch (option.id) {
                case kOptionChanges: options.verbosity = 1; break;
                case kOptionForce: options.force = true; break;
                case kOptionVerbose: options.verbosity = 2; break;
                case kOptionNoPreserveRoot: options.preserveRoot = false; break;
                case kOptionPreserveRoot: options.preserveRoot = true; break;
                case kOptionRecursive: options.recursive = true; break;
                case kOptionReference: referenceFile = option.argument; break;
                default: break;  // not treated (already reported)
            }
        }

        if (!referenceFile.empty() && !scannedMode.empty()) {
            context.Error("cannot combine mode and --reference options");
            context.TryHelp();
            return 1;
        }
        std::vector<std::string> files = parsed->operands;
        std::string modeSpec = scannedMode;
        if (referenceFile.empty()) {
            if (modeSpec.empty()) {
                if (files.empty()) {
                    context.Error("missing operand");
                    context.TryHelp();
                    return 1;
                }
                modeSpec = files.front();
                files.erase(files.begin());
            }
            if (files.empty()) {
                context.Error("missing operand after " + GnuQuote(modeSpec));
                context.TryHelp();
                return 1;
            }
            int validated = 0;
            if (!ParseChmodMode(modeSpec, kEveryoneMode, /*isDirectory=*/false, validated)) {
                context.Error("invalid mode: " + GnuQuote(modeSpec));
                context.TryHelp();
                return 1;
            }
            options.modeSpec = modeSpec;
        } else {
            if (files.empty()) {
                context.Error("missing operand");
                context.TryHelp();
                return 1;
            }
            options.fromReference = true;
            IFileIO& io = context.IO();
            FileStatus status;
            if (io.Stat(referenceFile, status) != 0) {
                context.Error("failed to get attributes of " + q(referenceFile) +
                    ": No such file or directory");
                return 1;
            }
        }

        int status = 0;
        for (const auto& file : files) {
            if (!ChmodFile(context, file, options)) {
                status = 1;
            }
        }
        return status;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateChmodCommand() {
    return std::make_shared<ChmodCommand>();
}

} // namespace Haisos