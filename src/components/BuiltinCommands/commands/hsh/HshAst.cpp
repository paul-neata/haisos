#include "commands/hsh/HshAst.h"

namespace Haisos::Hsh {

namespace {

const char* RedirectionOp(const Redirection& redir) {
    switch (redir.kind) {
        case RedirectionKind::Input:          return "<";
        case RedirectionKind::Output:         return ">";
        case RedirectionKind::OutputClobber:  return ">|";
        case RedirectionKind::Append:         return ">>";
        case RedirectionKind::ReadWrite:      return "<>";
        case RedirectionKind::DupInput:       return "<&";
        case RedirectionKind::DupOutput:      return ">&";
        case RedirectionKind::HereDoc:
            return (redir.hereDoc && redir.hereDoc->stripTabs) ? "<<-" : "<<";
        case RedirectionKind::HereString:     return "<<<";
        case RedirectionKind::OutputAndError: return "&>";
    }
    return "";
}

std::string DumpRedirection(const Redirection& redir) {
    if (redir.kind == RedirectionKind::OutputAndError) {
        return "&>" + redir.target.source;
    }
    return std::to_string(redir.fd) + RedirectionOp(redir) + redir.target.source;
}

std::string DumpPipeline(const Pipeline& pipeline) {
    std::string out = pipeline.negated ? "! " : "";
    bool first = true;
    for (const CommandPtr& command : pipeline.commands) {
        if (!first) {
            out += " | ";
        }
        out += DumpCommand(*command);
        first = false;
    }
    return out;
}

std::string DumpAndOr(const AndOrList& andOr) {
    std::string out;
    for (size_t i = 0; i < andOr.pipelines.size(); ++i) {
        if (i > 0) {
            out += andOr.operators[i - 1] == AndOrOperator::And ? " && " : " || ";
        }
        out += DumpPipeline(andOr.pipelines[i]);
    }
    return out;
}

// The kind-specific part of a command's dump; the redirections are handled by
// DumpCommand.
std::string DumpCommandBody(const Command& command) {
    switch (command.kind) {
        case CommandKind::Simple: {
            const auto& simple = static_cast<const SimpleCommand&>(command);
            std::string out = "[";
            bool first = true;
            auto element = [&](const std::string& text) {
                if (!first) {
                    out += ' ';
                }
                out += text;
                first = false;
            };
            for (const Assignment& assignment : simple.assignments) {
                element(assignment.name + "=" + assignment.value.source);
            }
            for (const Word& word : simple.words) {
                element(word.source);
            }
            for (const Redirection& redir : simple.redirections) {
                element(DumpRedirection(redir));
            }
            out += "]";
            return out;
        }
        case CommandKind::BraceGroup:
            return "{ " + DumpCommandList(static_cast<const BraceGroup&>(command).body) + " }";
        case CommandKind::Subshell:
            return "( " + DumpCommandList(static_cast<const Subshell&>(command).body) + " )";
        case CommandKind::If: {
            const auto& ifCommand = static_cast<const IfCommand&>(command);
            std::string out;
            bool first = true;
            for (const IfBranch& branch : ifCommand.branches) {
                out += first ? "if " : " elif ";
                out += DumpCommandList(branch.condition);
                out += " then ";
                out += DumpCommandList(branch.body);
                first = false;
            }
            if (ifCommand.elseBody) {
                out += " else ";
                out += DumpCommandList(*ifCommand.elseBody);
            }
            out += " fi";
            return out;
        }
        case CommandKind::While:
        case CommandKind::Until: {
            const auto& loop = static_cast<const LoopCommand&>(command);
            std::string out = command.kind == CommandKind::While ? "while " : "until ";
            out += DumpCommandList(loop.condition);
            out += " do ";
            out += DumpCommandList(loop.body);
            out += " done";
            return out;
        }
        case CommandKind::For: {
            const auto& forCommand = static_cast<const ForCommand&>(command);
            std::string out = "for " + forCommand.variable;
            if (forCommand.hasIn) {
                out += " in";
                for (const Word& word : forCommand.words) {
                    out += " " + word.source;
                }
            }
            out += " do ";
            out += DumpCommandList(forCommand.body);
            out += " done";
            return out;
        }
        case CommandKind::Case: {
            const auto& caseCommand = static_cast<const CaseCommand&>(command);
            std::string out = "case " + caseCommand.subject.source + " in";
            for (const CaseItem& item : caseCommand.items) {
                out += " ";
                bool first = true;
                for (const Word& pattern : item.patterns) {
                    if (!first) {
                        out += "|";
                    }
                    out += pattern.source;
                    first = false;
                }
                out += ")";
                if (!item.body.items.empty()) {
                    out += " " + DumpCommandList(item.body);
                }
                out += " ;;";
            }
            out += " esac";
            return out;
        }
        case CommandKind::FunctionDefinition: {
            const auto& function = static_cast<const FunctionDefinition&>(command);
            std::string out = function.name + "()";
            if (function.body) {
                out += " " + DumpCommand(*function.body);
            }
            return out;
        }
    }
    return "";
}

} // namespace

std::string DumpCommand(const Command& command) {
    std::string out = DumpCommandBody(command);
    if (command.kind != CommandKind::Simple) {
        for (const Redirection& redir : command.redirections) {
            out += " " + DumpRedirection(redir);
        }
    }
    return out;
}

std::string DumpCommandList(const CommandList& list) {
    std::string out;
    bool first = true;
    for (const ListItem& item : list.items) {
        if (!first) {
            out += "; ";
        }
        out += DumpAndOr(item.andOr);
        if (item.background) {
            out += " &";
        }
        first = false;
    }
    return out;
}

} // namespace Haisos::Hsh
