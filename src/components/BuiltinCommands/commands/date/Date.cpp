#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinDate.h"
#include "BuiltinText.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {

namespace {

constexpr int kOptionDate = 1;         // -d, --date=STRING
constexpr int kOptionFile = 2;         // -f, --file=DATEFILE
constexpr int kOptionIso = 3;          // -I[FMT], --iso-8601[=FMT]
constexpr int kOptionReference = 4;    // -r, --reference=FILE
constexpr int kOptionRfcEmail = 5;     // -R, --rfc-email
constexpr int kOptionRfc3339 = 6;      // --rfc-3339=FMT
constexpr int kOptionSet = 7;          // -s, --set=STRING
constexpr int kOptionUtc = 8;          // -u, --utc, --universal, --uct
constexpr int kOptionResolution = 9;   // --resolution

// The words -I and --rfc-3339 take, as ArgMatch values.
constexpr int kIsoDate = 1;
constexpr int kIsoHours = 2;
constexpr int kIsoMinutes = 3;
constexpr int kIsoSeconds = 4;
constexpr int kIsoNs = 5;

// The set-time operand, MMDDhhmm[[CC]YY][.ss], has its year at the end; it is
// rearranged into the [[CC]YY]MMDDhhmm[.ss] ParseTouchStamp takes.
std::string RearrangeSetStamp(const std::string& text) {
    const size_t dot = text.find('.');
    const std::string main = dot == std::string::npos ? text : text.substr(0, dot);
    const std::string fraction = dot == std::string::npos ? std::string() : text.substr(dot);
    if (main.size() == 10 || main.size() == 12) {
        return main.substr(8) + main.substr(0, 8) + fraction;
    }
    return text;  // not a stamp ParseTouchStamp could take either
}

// GNU's adjust_resolution: in --resolution mode %-N is %9N, so the
// resolution's digits come out in full.
std::string AdjustResolution(const std::string& format) {
    std::string adjusted = format;
    size_t at = adjusted.find("%-N");
    while (at != std::string::npos) {
        adjusted.replace(at, 3, "%9N");
        at = adjusted.find("%-N", at + 3);
    }
    return adjusted;
}

// Why -f's file could not be read, as GNU words it: a directory is one GNU
// opens and fails reading.
std::string FileFailureText(InputOpenFailure failure) {
    switch (failure) {
        case InputOpenFailure::Missing: return "No such file or directory";
        case InputOpenFailure::Directory: return "read error: Is a directory";
        case InputOpenFailure::Denied: return "Permission denied";
        default: return "Bad file descriptor";
    }
}

class DateCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "date"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        using A = BuiltinArgument;
        static const std::vector<BuiltinOption> options = {
            {'d', "date", kOptionDate, A::Required, "STRING", "display time described by STRING, not 'now'"},
            {0, "debug", kBuiltinNotTreated},
            {'f', "file", kOptionFile, A::Required, "DATEFILE", "like --date; once for each line of DATEFILE"},
            {'I', "iso-8601", kOptionIso, A::OptionalAttached, "FMT", "ISO 8601: date (the default), hours, minutes, seconds or ns"},
            {0, "resolution", kOptionResolution, A::None, "", "output the available resolution of timestamps"},
            {'R', "rfc-email", kOptionRfcEmail, A::None, "", "RFC 5322: Mon, 14 Aug 2006 02:34:56 -0600"},
            {0, "rfc-3339", kOptionRfc3339, A::Required, "FMT", "RFC 3339: date, seconds or ns"},
            {'r', "reference", kOptionReference, A::Required, "FILE", "display the last modification time of FILE"},
            {'s', "set", kOptionSet, A::Required, "STRING", "set time described by STRING (refused: no clock to set)"},
            {'u', "utc", kOptionUtc, A::None, "", "print or set Coordinated Universal Time (UTC)"},
            {0, "universal", kOptionUtc, A::None, "", "", /*hidden=*/true},
            {0, "uct", kOptionUtc, A::None, "", "", /*hidden=*/true},
            {0, "rfc-822", kOptionRfcEmail, A::None, "", "", /*hidden=*/true},
            {0, "rfc-2822", kOptionRfcEmail, A::None, "", "", /*hidden=*/true},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "print the system date and time",
            {"date [OPTION]... [+FORMAT]",
             "date [-u|--utc|--universal] [MMDDhhmm[[CC]YY][.ss]]"},
            "-d and -s strings, and the set-time operand, are parsed as touch -d's are\n"
            "(see `man touch`): no TZ=\"Zone\" items, no names for months or weekdays.\n"
            "Setting the time is refused (Operation not permitted): HaisosOS has no clock to set.\n"
            "The local zone is the host's; TZ is not consulted. On Windows %Z is the long zone name.\n"
            "--universal, --uct, --rfc-822 and --rfc-2822 are the same as --utc and -R.",
        };
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }

        // The ways a time to display can be given, and the one way to set it.
        // A repeat of the same kind takes the last one; any two different
        // kinds conflict, as GNU's "All options that specify the date to
        // display are mutually exclusive" says.
        enum Source { kSourceNone, kSourceDate, kSourceFile, kSourceReference, kSourceResolution, kSourceSet };
        Source source = kSourceNone;
        bool setFromStamp = false;  // the set time is a MMDDhhmm... operand, not -s's string
        std::string dateString, dateFile, referenceFile, setString;
        bool utc = false;
        bool resolution = false;
        // -I, --rfc-3339 and -R each choose an output format; a second one
        // (a +FORMAT operand included) is refused.
        int formatsGiven = 0;
        int isoFormat = 0, rfc3339Format = 0;
        bool rfcEmail = false;

        // Any two different time sources conflict; setting is its own kind,
        // with its own message.
        const auto sourceConflicts = [&](Source next) {
            if (source == kSourceNone || source == next) {
                return false;
            }
            context.Error(source == kSourceSet || next == kSourceSet
                ? "the options to print and set the time may not be used together"
                : "the options to specify dates for printing are mutually exclusive");
            context.TryHelp();
            return true;
        };
        const auto formatConflicts = [&]() {
            if (++formatsGiven > 1) {
                context.Error("multiple output formats specified");
                return true;
            }
            return false;
        };

        for (const auto& option : parsed->options) {
            switch (option.id) {
                case kOptionDate:
                    if (sourceConflicts(kSourceDate)) {
                        return 1;
                    }
                    source = kSourceDate;
                    dateString = option.argument;
                    break;
                case kOptionFile:
                    if (sourceConflicts(kSourceFile)) {
                        return 1;
                    }
                    source = kSourceFile;
                    dateFile = option.argument;
                    break;
                case kOptionReference:
                    if (sourceConflicts(kSourceReference)) {
                        return 1;
                    }
                    source = kSourceReference;
                    referenceFile = option.argument;
                    break;
                case kOptionResolution:
                    if (sourceConflicts(kSourceResolution)) {
                        return 1;
                    }
                    source = kSourceResolution;
                    resolution = true;
                    break;
                case kOptionSet:
                    if (sourceConflicts(kSourceSet)) {
                        return 1;
                    }
                    source = kSourceSet;
                    setString = option.argument;
                    break;
                case kOptionUtc:
                    utc = true;
                    break;
                case kOptionIso: {
                    static const std::vector<ArgChoice> kIsoFormats = {
                        {"hours", kIsoHours},
                        {"minutes", kIsoMinutes},
                        {"date", kIsoDate},
                        {"seconds", kIsoSeconds},
                        {"ns", kIsoNs},
                    };
                    // -I alone is -Idate: the FMT word may only be attached.
                    if (option.argument.empty()) {
                        isoFormat = kIsoDate;
                    } else {
                        const auto matched = ArgMatch(context, "--iso-8601", option.argument, kIsoFormats);
                        if (!matched) {
                            return 1;
                        }
                        isoFormat = *matched;
                    }
                    if (formatConflicts()) {
                        return 1;
                    }
                    break;
                }
                case kOptionRfc3339: {
                    static const std::vector<ArgChoice> kRfc3339Formats = {
                        {"date", kIsoDate},
                        {"seconds", kIsoSeconds},
                        {"ns", kIsoNs},
                    };
                    const auto matched = ArgMatch(context, "--rfc-3339", option.argument, kRfc3339Formats);
                    if (!matched) {
                        return 1;
                    }
                    rfc3339Format = *matched;
                    if (formatConflicts()) {
                        return 1;
                    }
                    break;
                }
                case kOptionRfcEmail:
                    if (formatConflicts()) {
                        return 1;
                    }
                    rfcEmail = true;
                    break;
                default:
                    break;  // --debug: not treated, already reported
            }
        }

        // One operand, no more: a +FORMAT, or else -- when no option gave the
        // time -- the MMDDhhmm[[CC]YY][.ss] to set the clock to.
        std::string formatOperand;
        bool formatGiven = false;
        if (parsed->operands.size() > 1) {
            context.Error("extra operand " + GnuQuote(parsed->operands[1]));
            context.TryHelp();
            return 1;
        }
        if (parsed->operands.size() == 1) {
            const std::string& operand = parsed->operands[0];
            if (!operand.empty() && operand[0] == '+') {
                formatGiven = true;
                formatOperand = operand.substr(1);
            } else if (source != kSourceNone) {
                context.ErrorText(context.Name() + ": the argument " + GnuQuote(operand)
                    + " lacks a leading '+';\n"
                      "when using an option to specify date(s), any non-option\n"
                      "argument must be a format string beginning with '+'\n");
                context.TryHelp();
                return 1;
            } else {
                source = kSourceSet;
                setFromStamp = true;
                setString = operand;
            }
        }
        if (formatGiven && formatConflicts()) {
            return 1;
        }

        // The format, as the options (or the operand) chose it.
        std::string format = "%a %b %e %H:%M:%S %Z %Y";  // GNU's default, in the C locale
        if (rfcEmail) {
            format = "%a, %d %b %Y %H:%M:%S %z";
        } else if (rfc3339Format != 0) {
            format = rfc3339Format == kIsoDate ? "%Y-%m-%d"
                : rfc3339Format == kIsoSeconds ? "%Y-%m-%d %H:%M:%S%:z"
                                               : "%Y-%m-%d %H:%M:%S.%N%:z";
        } else if (isoFormat != 0) {
            format = isoFormat == kIsoDate ? "%Y-%m-%d"
                : isoFormat == kIsoHours ? "%Y-%m-%dT%H%:z"
                : isoFormat == kIsoMinutes ? "%Y-%m-%dT%H:%M%:z"
                : isoFormat == kIsoSeconds ? "%Y-%m-%dT%H:%M:%S%:z"
                                          : "%Y-%m-%dT%H:%M:%S,%N%:z";
        } else if (resolution) {
            format = "%s.%N";
        }
        if (formatGiven) {
            format = formatOperand;
        }
        if (resolution) {
            format = AdjustResolution(format);
        }

        const FileDateTime now = CurrentFileDateTime();
        const auto printDate = [&](const FileDateTime& when) {
            context.Out(FormatDateTime(format, when, utc) + "\n");
        };

        switch (source) {
            case kSourceSet: {
                FileDateTime when;
                const bool parsedOk = setFromStamp
                    ? ParseTouchStamp(RearrangeSetStamp(setString), now, utc, when)
                    : ParseDateString(setString, now, utc, when);
                if (!parsedOk) {
                    context.Error("invalid date " + GnuQuote(setString));
                    return 1;
                }
                // GNU's unprivileged date prints the new time and fails to
                // set it; HaisosOS has no clock to set at all.
                printDate(when);
                context.Error("cannot set date: Operation not permitted");
                return 1;
            }
            case kSourceDate: {
                FileDateTime when;
                if (!ParseDateString(dateString, now, utc, when)) {
                    context.Error("invalid date " + GnuQuote(dateString));
                    return 1;
                }
                printDate(when);
                return 0;
            }
            case kSourceReference: {
                FileStatus status;
                if (context.IO().Stat(referenceFile, status) != 0) {
                    context.Error(ShellEscapeQuoted(referenceFile) + ": No such file or directory");
                    return 1;
                }
                printDate(status.modificationTime);
                return 0;
            }
            case kSourceResolution:
                // HaisosOS's clock reaches the nanosecond: the point {0, 1}
                // prints as "0.000000001".
                printDate(FileDateTime{0, 1});
                return 0;
            case kSourceFile: {
                InputOpenFailure failure = InputOpenFailure::None;
                auto input = OpenInputOperand(context, dateFile, failure);
                if (!input) {
                    context.Error(ShellEscapeQuoted(dateFile) + ": " + FileFailureText(failure));
                    return 1;
                }
                BuiltinLineReader reader(context, *input, '\n');
                std::string line;
                bool delimited = false;
                int status = 0;
                while (true) {
                    const LineReadResult result = reader.Next(line, delimited);
                    if (result == LineReadResult::End) {
                        return status;
                    }
                    if (result == LineReadResult::Stopped) {
                        return 1;
                    }
                    if (result == LineReadResult::Error) {
                        context.Error(ShellEscapeQuoted(dateFile) + ": read error: Input/output error");
                        return 1;
                    }
                    FileDateTime when;
                    if (!ParseDateString(line, now, utc, when)) {
                        context.Error("invalid date " + GnuQuote(line));
                        status = 1;
                        continue;
                    }
                    printDate(when);
                }
            }
            default:
                printDate(now);
                return 0;
        }
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateDateCommand() {
    return std::make_shared<DateCommand>();
}

} // namespace Haisos