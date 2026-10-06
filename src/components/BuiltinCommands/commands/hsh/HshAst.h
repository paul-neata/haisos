#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "commands/hsh/HshWord.h"

namespace Haisos::Hsh {

enum class RedirectionKind {
    Input,          // [n]<word    default fd 0
    Output,         // [n]>word    default 1
    OutputClobber,  // [n]>|word   default 1
    Append,         // [n]>>word   default 1
    ReadWrite,      // [n]<>word   default 0
    DupInput,       // [n]<&word   default 0; word: a digit, or "-" to close
    DupOutput,      // [n]>&word   default 1; word: a digit, or "-" to close
    HereDoc,        // [n]<<word, [n]<<-word   default 0
    HereString,     // [n]<<<word  default 0 (bash): the word and a newline as input
    OutputAndError, // &>word      (bash) stdout and stderr to the file; fd is 1
};

struct Redirection {
    RedirectionKind kind = RedirectionKind::Input;
    int fd = 0;                             // the IO number given, else the kind's default
    Word target;                            // the file, the dup word, the here-string, or the heredoc's delimiter word
    std::shared_ptr<HereDocument> hereDoc;  // HereDoc only (shared with the lexer, which filled it)
    int line = 0;
};

struct Assignment {
    std::string name;  // a valid name
    Word value;        // what follows "name=" (may have no parts); value.source is the text after '='
};

enum class CommandKind { Simple, BraceGroup, Subshell, If, While, Until, For, Case, FunctionDefinition };

struct CommandList;  // below

struct Command {
    explicit Command(CommandKind kind) : kind(kind) {}
    virtual ~Command() = default;
    CommandKind kind;
    int line = 0;  // the line of the command's first token
    // Simple: every redirection, in the order written (before, between or
    // after the words). Compound commands: those written after it
    // (`{ ...; } >f`). FunctionDefinition: always empty -- redirections after
    // the body belong to the body and apply on every call.
    std::vector<Redirection> redirections;
};
using CommandPtr = std::shared_ptr<Command>;

struct Pipeline {
    bool negated = false;              // ! in front
    std::vector<CommandPtr> commands;  // one or more, joined by |
    int line = 0;
};

enum class AndOrOperator { And, Or };  // &&, ||

struct AndOrList {
    std::vector<Pipeline> pipelines;        // one or more
    std::vector<AndOrOperator> operators;   // operators[i] joins pipelines[i] and pipelines[i + 1]
};

struct ListItem {
    AndOrList andOr;
    bool background = false;  // ended by &
    // The and-or list exactly as written, re-parsable on its own to the same
    // command (a background item needing the shell is run as `hsh -c
    // <sourceText>`): see "Source text" in HshParser.h.
    std::string sourceText;
};

struct CommandList {
    std::vector<ListItem> items;  // may be empty (a case item's body)
};

struct SimpleCommand : Command {
    SimpleCommand() : Command(CommandKind::Simple) {}
    std::vector<Assignment> assignments;  // the leading name=value words
    std::vector<Word> words;              // the command name and its arguments; may be empty (`x=1`, `>f`)
};
struct BraceGroup : Command { BraceGroup() : Command(CommandKind::BraceGroup) {} CommandList body; };
struct Subshell : Command { Subshell() : Command(CommandKind::Subshell) {} CommandList body; };

struct IfBranch { CommandList condition; CommandList body; };
struct IfCommand : Command {
    IfCommand() : Command(CommandKind::If) {}
    std::vector<IfBranch> branches;          // the if, then each elif
    std::optional<CommandList> elseBody;     // set when there is an else
};

// while and until (kind While or Until): repeat body while condition succeeds / fails.
struct LoopCommand : Command {
    explicit LoopCommand(CommandKind kind) : Command(kind) {}
    CommandList condition;
    CommandList body;
};

struct ForCommand : Command {
    ForCommand() : Command(CommandKind::For) {}
    std::string variable;
    bool hasIn = false;       // `for x in ...`; without `in` the loop runs over "$@"
    std::vector<Word> words;  // the words after `in` (possibly none)
    CommandList body;
};

struct CaseItem {
    std::vector<Word> patterns;  // pattern | pattern ...
    CommandList body;            // possibly empty
};
struct CaseCommand : Command {
    CaseCommand() : Command(CommandKind::Case) {}
    Word subject;
    std::vector<CaseItem> items;
};

struct FunctionDefinition : Command {
    FunctionDefinition() : Command(CommandKind::FunctionDefinition) {}
    std::string name;
    // Any command, as dash allows (`f() echo hi` too), with its redirections.
    // Shared so the function table can keep it after the tree is gone.
    CommandPtr body;
    // The whole definition exactly as written, `name() compound-command
    // [redirections]`, re-parsable on its own: see "Source text" in HshParser.h.
    std::string sourceText;
};

// The dump the tests compare against; the format is documented in the hsh
// CLAUDE.md ("AST dump").
std::string DumpCommandList(const CommandList& list);
std::string DumpCommand(const Command& command);

} // namespace Haisos::Hsh
