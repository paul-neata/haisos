#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "interfaces/IFileSystemService.h"
#include "src/components/Regex/Regex.h"

namespace Haisos::Find {

// One file being looked at, as the walk hands it to every primary.
struct FindFile {
    std::string path;           // as find prints it: ".", "./a", "a//x", "/"
    std::string name;           // its base name, as -name sees it
    std::string startingPoint;  // the starting point it was found under, as given
    int depth = 0;              // 0 for a starting point itself
    FileStatus status;          // the walk's IFileIO::Stat of it
};

// The global options (-depth, -maxdepth, ...), filled in by FindParser.
struct FindSettings {
    std::vector<std::string> startingPoints;  // "." when none is given and there is no -files0-from
    bool depthFirst = false;                  // -depth, -d
    bool depthGiven = false;                  // -depth or -d was written explicitly
    int maxDepth = -1;                        // no limit while negative
    int minDepth = 0;
    bool ignoreReaddirRace = false;
    std::optional<std::string> files0From;     // -files0-from FILE
};

// What every primary is handed while the tree is walked.
struct FindRun {
    BuiltinContext& context;
    FindSettings& settings;
    int exitStatus = 0;   // becomes 1 on any reported error
    bool quit = false;    // -quit: end the whole walk after this file
    bool prune = false;   // -prune: do not descend into the current file
};

// One primary of the expression: a test, an action, or an option (always
// true). Plain classes, held by unique_ptr in the tree.
class FindPrimary {
public:
    virtual ~FindPrimary() = default;
    virtual bool Evaluate(FindRun& run, const FindFile& file) = 0;
    // Once, after the walk ends (normally or by -quit): -exec ... + runs its
    // last batch here, -fprint flushes its file. Default: nothing.
    virtual void Finish(FindRun& run) {}
};

// The expression tree. And/Or short-circuit (a -quit reached on the left
// stops the whole walk); Comma evaluates both and gives the right side;
// Not negates its left.
struct FindNode {
    enum class Kind { And, Or, Not, Comma, Primary };
    Kind kind = Kind::Primary;
    std::unique_ptr<FindNode> left;    // And/Or/Comma: both; Not: left only
    std::unique_ptr<FindNode> right;
    std::unique_ptr<FindPrimary> primary;
};

bool EvaluateFindNode(FindNode& node, FindRun& run, const FindFile& file);

class FindParser;

enum class FindPrimaryKind { GlobalOption, PositionalOption, Test, Action };

struct FindPrimaryEntry {
    const char* name;             // "-name", as spelled on the command line
    FindPrimaryKind kind;
    bool suppressesDefaultPrint;  // true of every action but -prune and -quit
    // Reads the primary's arguments through |parser| and returns it. An
    // option that only changes settings returns a primary that is always
    // true. Null after parser.Fail(...); -help and -version also return null,
    // with the parser ending the whole parse with exit status 0 instead.
    std::function<std::unique_ptr<FindPrimary>(FindParser& parser, const std::string& name)> parse;
};

// The rows of the tests' primaries (FindTests.cpp) and of the actions'
// (FindActions.cpp), GNU find's table order. FindParser looks a primary up
// in the tests' rows first, then in the actions'.
const std::vector<FindPrimaryEntry>& FindTestPrimaries();
const std::vector<FindPrimaryEntry>& FindActionPrimaries();

// What the primaries' parse functions read and change while parsing.
class FindActionState;  // the actions' own shared state (FindActions.cpp)

struct FindParseState {
    bool warnings = false;                      // -warn/-nowarn; default: stdin is a terminal
    FileDateTime startTime;                     // CurrentFileDateTime() when find started
    FileDateTime timeOrigin;                    // startTime, or after -daystart the start of tomorrow local
    RegexSyntax regexSyntax = RegexSyntax::Basic;  // from -regextype
    bool regexEmacs = true;                     // the default type, emacs (translated, FindTests.cpp)
    // What the actions share: -ok and -okdir's one prompt, the output files
    // -fprint and friends open once for every primary naming them, and
    // whether -delete was given. Null until an action is parsed.
    std::shared_ptr<FindActionState> actions;
};

// Whether -delete was parsed among the actions (false while no action was):
// FindParser checks its clash with -prune once the tree is built.
bool FindDeleteWasGiven(const FindParseState& state);

class FindParser {
public:
    FindParser(BuiltinContext& context, const std::vector<std::string>& args);
    ~FindParser();
    FindParser(const FindParser&) = delete;
    FindParser& operator=(const FindParser&) = delete;

    // Parses everything: the leading options, the starting points and the
    // expression. Returns true with |expression| (null: no expression) and
    // |anyAction|; false when find must end now with |exitStatus| (1 after an
    // error, 0 after -help/-version).
    bool Parse(FindSettings& settings, std::unique_ptr<FindNode>& expression,
               bool& anyAction, int& exitStatus);

    // For the rows of FindTestPrimaries():

    // The primary's next argument, or Fail("missing argument to `-x'").
    bool NextArgument(const std::string& primaryName, std::string& out);
    // Whether the command line has one more argument to give: -newerXY's
    // own missing-argument message needs the look before the take.
    bool HasArgument() const;
    void Fail(const std::string& message);  // context.Error(message); the parse fails, exit 1
    void Warn(const std::string& message);  // context.Error("warning: " + message) when warnings are on
    BuiltinContext& Context();
    FindSettings& Settings();
    FindParseState& State();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace Haisos::Find