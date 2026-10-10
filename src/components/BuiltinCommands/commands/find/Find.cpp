// The find command itself: the option table every generic builtin test
// reads, the help, and the walk -- the expression itself (the tests and the
// options in FindTests.cpp, the actions in FindActions.cpp, the parsing of
// both in FindParser.cpp) lives there.
#include <memory>
#include <string>
#include <vector>

#include "BuiltinCommand.h"
#include "BuiltinText.h"
#include "commands/find/FindExpression.h"
#include "interfaces/IFileIO.h"

namespace Haisos::Find {
namespace {

// GNU's own limit on a whole path: the walk does not compose a child's
// name past it, but reports the too-long path instead.
constexpr size_t kMaxPathLength = 4096;

class FindCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "find"; }
    std::string Version() const override { return "1.1.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'H', "", 1, BuiltinArgument::None, "",
                "Follow a symbolic link given on the command line (HaisosOS has none: no effect)"},
            {'L', "", 2, BuiltinArgument::None, "",
                "Follow every symbolic link (HaisosOS has none: no effect)"},
            {'P', "", 3, BuiltinArgument::None, "",
                "Never follow a symbolic link (HaisosOS has none: no effect)"},
            {'O', "", kBuiltinNotTreated, BuiltinArgument::OptionalAttached, "LEVEL", ""},
            {'D', "", kBuiltinNotTreated, BuiltinArgument::Required, "DEBUGOPTS", ""},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        BuiltinHelp help;
        help.summary = "search for files in a directory hierarchy";
        help.usage = {"find [-H] [-L] [-P] [-Olevel] [-D debugopts] [starting-point...] [expression]"};
        help.notes =
            "The default starting-point is the current directory; the default expression\n"
            "is -print. Operators, highest precedence first (-a is implicit between two\n"
            "operands):\n"
            "  ( EXPR )   ! EXPR   -not EXPR   EXPR1 EXPR2   EXPR1 -a EXPR2   EXPR1 -and EXPR2\n"
            "  EXPR1 -o EXPR2   EXPR1 -or EXPR2   EXPR1 , EXPR2\n"
            "Positional options (always true): -daystart -follow -nowarn -regextype -warn.\n"
            "Normal options (always true): -depth -files0-from FILE -maxdepth N -mindepth N\n"
            "  -mount -noleaf -xdev -ignore_readdir_race -noignore_readdir_race\n"
            "Tests (N can be +N or -N or N): -amin -anewer -atime -cmin -cnewer -ctime\n"
            "  -empty -false -fstype -gid -group -ilname -iname -inum -ipath -iwholename\n"
            "  -iregex -links -lname -mmin -mtime -name -newer -nouser -nogroup -path\n"
            "  -perm -readable -writable -executable -regex -samefile -size -true -type\n"
            "  -uid -used -user -wholename -xtype\n"
            "Actions: -delete -exec -execdir -fls -fprint -fprint0 -fprintf -ls -ok\n"
            "  -okdir -print -print0 -printf -prune -quit.\n"
            "\n"
            "HaisosOS reports no owners or permissions: every file's mode is taken as\n"
            "0777, its user and group haisos (uid and gid 0), its inode number 0, and\n"
            "there are no symbolic links, so -lname never matches, %Y is %y and %l is\n"
            "empty, and -H -L -P -follow change nothing. A -newerXY with B (birth\n"
            "time) cannot be given, -type D is refused (no Solaris doors), and %B and\n"
            "%Z print nothing (no birth times, no SELinux). -ok and -okdir prompt on\n"
            "the standard error and read the answer from the standard input; -exec\n"
            "... + runs its command in batches of at most 131072 bytes.\n"
            "Not treated primaries: -mount, -xdev, -fstype";
        return help;
    }

    int Run(BuiltinContext& context) override {
        FindSettings settings;
        FindParser parser(context, context.Args());
        std::unique_ptr<FindNode> expression;
        bool anyAction = false;
        int exitStatus = 0;
        if (!parser.Parse(settings, expression, anyAction, exitStatus)) {
            return exitStatus;
        }
        FindRun run{context, settings};

        // No expression: -print. An expression with no action that prints of
        // its own: the expression and -print, as GNU's implicit one.
        if (!expression) {
            expression = DefaultPrint(context);
        } else if (!anyAction) {
            auto print = DefaultPrint(context);
            auto andNode = std::make_unique<FindNode>();
            andNode->kind = FindNode::Kind::And;
            andNode->left = std::move(expression);
            andNode->right = std::move(print);
            expression = std::move(andNode);
        }

        if (settings.files0From) {
            WalkFiles0From(run, *expression);
        } else {
            if (settings.startingPoints.empty()) {
                settings.startingPoints.push_back(".");
            }
            for (const std::string& point : settings.startingPoints) {
                VisitStartingPoint(run, point, *expression);
                if (run.quit) {
                    break;
                }
            }
        }
        FinishAll(*expression, run);
        return run.exitStatus;
    }

private:
    // The implicit -print, taken from the primaries table itself, by parsing
    // the expression "-print" -- the same way a user would have given it.
    static std::unique_ptr<FindNode> DefaultPrint(BuiltinContext& context) {
        static const std::vector<std::string> kArgs = {"-print"};
        FindSettings settings;
        FindParser parser(context, kArgs);
        std::unique_ptr<FindNode> expression;
        bool anyAction = false;
        int exitStatus = 0;
        if (parser.Parse(settings, expression, anyAction, exitStatus) && expression) {
            return expression;
        }
        return nullptr;
    }

    static void WalkFiles0From(FindRun& run, FindNode& expression) {
        const std::string& operand = *run.settings.files0From;
        if (!run.settings.startingPoints.empty()) {
            run.context.Error("extra operand " + GnuQuote(run.settings.startingPoints[0]));
            run.context.Error("file operands cannot be combined with -files0-from");
            run.exitStatus = 1;
            return;
        }
        const std::string display = operand == "-" ? "(standard input)" : operand;
        InputOpenFailure failure = InputOpenFailure::None;
        auto input = OpenInputOperand(run.context, operand, failure);
        if (!input) {
            switch (failure) {
            case InputOpenFailure::Missing:
                run.context.Error("cannot open " + GnuQuote(operand)
                    + " for reading: No such file or directory");
                break;
            case InputOpenFailure::Directory:
                // GNU opens a directory and fails on the read, as this words it.
                run.context.Error(GnuQuote(display) + ": read error: Is a directory");
                break;
            case InputOpenFailure::Denied:
                run.context.Error("cannot open " + GnuQuote(operand)
                    + " for reading: Permission denied");
                break;
            default:
                run.context.Error(GnuQuote(display) + ": read error: Bad file descriptor");
                break;
            }
            run.exitStatus = 1;
            return;
        }
        // One name at a time: each starting point is walked before the next
        // name is read, as GNU's iterator does.
        BuiltinLineReader reader(run.context, *input, '\0');
        for (int number = 0;;) {
            std::string name;
            bool delimited = false;
            const LineReadResult result = reader.Next(name, delimited);
            if (result == LineReadResult::End) {
                break;
            }
            if (result == LineReadResult::Stopped) {
                run.exitStatus = 1;
                break;
            }
            if (result == LineReadResult::Error) {
                run.context.Error(GnuQuote(display) + ": read error");
                run.exitStatus = 1;
                break;
            }
            ++number;
            if (name.empty()) {
                // A zero-length name, with its record number, as GNU's
                // filename:line-number shape.
                run.context.Error(GnuQuote(display) + ":" + std::to_string(number)
                    + ": invalid zero-length file name");
                run.exitStatus = 1;
                continue;
            }
            VisitStartingPoint(run, name, expression);
            if (run.quit) {
                break;
            }
        }
    }

    static void VisitStartingPoint(FindRun& run, const std::string& point, FindNode& expression) {
        FindFile file;
        file.path = point;
        file.startingPoint = point;
        // An empty name is no file, as GNU's lstat("") says -- not the
        // working directory the path resolution would make of it.
        if (point.empty() || run.context.IO().Stat(point, file.status) != 0) {
            run.context.Error(GnuQuote(point) + ": No such file or directory");
            run.exitStatus = 1;
            return;
        }
        // The starting point's name: everything after the last '/' once the
        // trailing ones are dropped ("." keeps its own, "/" is all it has).
        std::string trimmed = point;
        while (trimmed.size() > 1 && trimmed.back() == '/') {
            trimmed.pop_back();
        }
        const size_t slash = trimmed.rfind('/');
        file.name = slash == std::string::npos ? trimmed
            : (trimmed.size() == slash + 1 ? trimmed : trimmed.substr(slash + 1));
        Visit(run, file, expression);
    }

    // One file: evaluated, then descended into (before, with -depth), the
    // children in the order the directory lists them. False once the walk
    // is over: -quit was hit, or the process was asked to stop.
    static bool Visit(FindRun& run, const FindFile& file, FindNode& expression) {
        if (run.context.StopRequested()) {
            run.exitStatus = 1;
            return false;
        }
        run.prune = false;
        if (!run.settings.depthFirst && file.depth >= run.settings.minDepth) {
            EvaluateFindNode(expression, run, file);
            if (run.quit) {
                return false;
            }
        }
        if (file.status.type == DirectoryEntryType::Dir
            && !run.prune
            && (run.settings.maxDepth < 0 || file.depth < run.settings.maxDepth)) {
            IFileIO& io = run.context.IO();
            for (const DirectoryEntry& entry : io.ReadDirectory(file.path)) {
                if (entry.name == "." || entry.name == "..") {
                    continue;
                }
                FindFile child;
                child.path = file.path.back() == '/' ? file.path + entry.name
                    : file.path + "/" + entry.name;
                if (child.path.size() > kMaxPathLength) {
                    run.context.Error(GnuQuote(child.path) + ": File name too long");
                    run.exitStatus = 1;
                    continue;
                }
                if (io.Stat(child.path, child.status) != 0) {
                    // A file gone between the listing and the stat: the
                    // race -ignore_readdir_race keeps quiet about.
                    if (!run.settings.ignoreReaddirRace) {
                        run.context.Error(GnuQuote(child.path) + ": No such file or directory");
                        run.exitStatus = 1;
                    }
                    continue;
                }
                child.name = entry.name;
                child.startingPoint = file.startingPoint;
                child.depth = file.depth + 1;
                if (!Visit(run, child, expression)) {
                    return false;
                }
            }
        }
        if (run.settings.depthFirst && file.depth >= run.settings.minDepth) {
            run.prune = false;  // a child's -prune is not its parent's
            EvaluateFindNode(expression, run, file);
        }
        return !run.quit;
    }

    // Once, after the walk ends: the actions' own finishing (-exec ... +'s
    // last batch, in the order the expression names them).
    static void FinishAll(FindNode& node, FindRun& run) {
        if (node.left) {
            FinishAll(*node.left, run);
        }
        if (node.right) {
            FinishAll(*node.right, run);
        }
        if (node.primary) {
            node.primary->Finish(run);
        }
    }
};

} // namespace

} // namespace Haisos::Find

namespace Haisos {

std::shared_ptr<IBuiltinCommand> CreateFindCommand() {
    return std::make_shared<Find::FindCommand>();
}

} // namespace Haisos