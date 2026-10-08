#include <optional>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinCopy.h"
#include "BuiltinDate.h"
#include "BuiltinText.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {

namespace {

constexpr int kOptionAccess = 1;        // -a
constexpr int kOptionNoCreate = 2;      // -c, --no-create
constexpr int kOptionDate = 3;           // -d, --date=STRING
constexpr int kOptionIgnored = 4;        // -f
constexpr int kOptionNoDereference = 5;  // -h, --no-dereference
constexpr int kOptionModification = 6;   // -m
constexpr int kOptionReference = 7;      // -r, --reference=FILE
constexpr int kOptionStamp = 8;          // -t STAMP
constexpr int kOptionTime = 9;            // --time=WORD

class TouchCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "touch"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'a', "", kOptionAccess, BuiltinArgument::None, "", "change only the access time"},
            {'c', "no-create", kOptionNoCreate, BuiltinArgument::None, "", "do not create any files"},
            {'d', "date", kOptionDate, BuiltinArgument::Required, "STRING", "parse STRING and use it instead of current time"},
            {'f', "", kOptionIgnored, BuiltinArgument::None, "", "(ignored)"},
            {'h', "no-dereference", kOptionNoDereference, BuiltinArgument::None, "", "the same: HaisosOS has no links"},
            {'m', "", kOptionModification, BuiltinArgument::None, "", "change only the modification time"},
            {'r', "reference", kOptionReference, BuiltinArgument::Required, "FILE", "use this file's times instead of current time"},
            {'t', "", kOptionStamp, BuiltinArgument::Required, "STAMP", "use [[CC]YY]MMDDhhmm[.ss] instead of current time"},
            {0, "time", kOptionTime, BuiltinArgument::Required, "WORD", "change only access or modification time, as WORD says"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "change file timestamps",
            {"touch [OPTION]... FILE..."},
            "- is not touched: HaisosOS has no file open on standard output.\n"
            "-h is the same as without it: there are no links.\n"
            "Times are set to the second; the change time is always now."};
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }

        bool accessOnly = false;
        bool modificationOnly = false;
        bool noCreate = false;
        std::string dateString, stampString, referenceFile;
        bool dateGiven = false, stampGiven = false, referenceGiven = false;

        for (const auto& option : parsed->options) {
            switch (option.id) {
                case kOptionAccess: accessOnly = true; break;
                case kOptionNoCreate: noCreate = true; break;
                case kOptionDate: dateString = option.argument; dateGiven = true; break;
                case kOptionNoDereference: break;  // no links: nothing to dereference
                case kOptionModification: modificationOnly = true; break;
                case kOptionReference: referenceFile = option.argument; referenceGiven = true; break;
                case kOptionStamp: stampString = option.argument; stampGiven = true; break;
                case kOptionTime: {
                    static const std::vector<ArgChoice> kTimes = {
                        {"atime", 1}, {"access", 1}, {"use", 1},
                        {"mtime", 2}, {"modify", 2},
                    };
                    const auto matched = ArgMatch(context, "--time", option.argument, kTimes);
                    if (!matched) {
                        return 1;
                    }
                    if (*matched == 1) {
                        accessOnly = true;
                    } else {
                        modificationOnly = true;
                    }
                    break;
                }
                default: break;  // not treated (already reported); -f ignored
            }
        }

        if (parsed->operands.empty()) {
            context.Error("missing file operand");
            context.TryHelp();
            return 1;
        }

        // -d, -t and -r are time sources; -d together with -r is the one
        // combination GNU allows, the -d string then relative to the
        // reference's times.
        if ((stampGiven && dateGiven) || (stampGiven && referenceGiven)) {
            context.Error("cannot specify times from more than one source");
            context.TryHelp();
            return 1;
        }

        IFileIO& io = context.IO();

        // The two times every operand gets. Without a source, now, taken
        // once for them all.
        FileDateTime accessValue, modificationValue;
        if (referenceGiven) {
            FileStatus referenceStatus;
            if (io.Stat(referenceFile, referenceStatus) != 0) {
                context.Error("failed to get attributes of " + ShellEscapeQuoted(referenceFile, /*always=*/true)
                    + ": No such file or directory");
                return 1;
            }
            accessValue = referenceStatus.accessTime;
            modificationValue = referenceStatus.modificationTime;
            if (dateGiven) {
                // Parsed twice, as GNU does: each time from its own base,
                // so relative items count from the reference's two times.
                if (!ParseDateString(dateString, accessValue, accessValue) ||
                    !ParseDateString(dateString, modificationValue, modificationValue)) {
                    context.Error("invalid date format " + ShellEscapeQuoted(dateString, /*always=*/true));
                    return 1;
                }
            }
        } else if (stampGiven) {
            const FileDateTime now = CurrentFileDateTime();
            if (!ParseTouchStamp(stampString, now, accessValue)) {
                context.Error("invalid date format " + ShellEscapeQuoted(stampString, /*always=*/true));
                return 1;
            }
            modificationValue = accessValue;
        } else if (dateGiven) {
            const FileDateTime now = CurrentFileDateTime();
            if (!ParseDateString(dateString, now, accessValue)) {
                context.Error("invalid date format " + ShellEscapeQuoted(dateString, /*always=*/true));
                return 1;
            }
            modificationValue = accessValue;
        } else {
            accessValue = CurrentFileDateTime();
            modificationValue = accessValue;
        }

        // Neither -a nor -m changes both; --time is their word form.
        std::optional<FileDateTime> accessTime = accessValue;
        std::optional<FileDateTime> modificationTime = modificationValue;
        if (accessOnly && !modificationOnly) {
            modificationTime = std::nullopt;
        }
        if (modificationOnly && !accessOnly) {
            accessTime = std::nullopt;
        }

        int status = 0;
        for (const std::string& path : parsed->operands) {
            if (context.StopRequested()) {
                return 1;
            }
            if (path == "-") {
                // GNU touches the file open on standard output; Haisos has
                // none, so "-" is quietly left alone.
                continue;
            }
            if (!EntryTypeOf(io, path)) {
                if (noCreate) {
                    continue;
                }
                // Never truncating: creating the file is all.
                auto file = io.OpenFile(path, kFileOpenWriteCreateAppend, kFileCreateMode);
                if (!file) {
                    context.Error("cannot touch " + ShellEscapeQuoted(path, /*always=*/true)
                        + ": " + CopyCreateFailedReason(io, path));
                    status = 1;
                    continue;
                }
            }
            if (io.SetTimes(path, accessTime, modificationTime) != 0) {
                context.Error("setting times of " + ShellEscapeQuoted(path, /*always=*/true)
                    + ": Permission denied");
                status = 1;
            }
        }
        return status;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateTouchCommand() {
    return std::make_shared<TouchCommand>();
}

} // namespace Haisos