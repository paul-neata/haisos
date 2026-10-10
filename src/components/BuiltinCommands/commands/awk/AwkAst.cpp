#include "commands/awk/AwkAst.h"

#include <cstdio>

namespace Haisos::Awk {

bool IsLvalue(const Expr& expr) {
    return expr.kind == ExprKind::Variable || expr.kind == ExprKind::Field ||
           expr.kind == ExprKind::Index;
}

namespace {

// A string literal's value escaped for a dump: \\ \" \n \t, and every other
// byte below 0x20 or 0x7F as three octal digits (\000) -- the same escaping
// DescribeToken gives a STR token.
std::string DumpString(const std::string& text) {
    std::string out = "\"";
    char buffer[8];
    for (const char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20 || c == '\x7f') {
                    std::snprintf(buffer, sizeof(buffer), "\\%03o", static_cast<unsigned char>(c));
                    out += buffer;
                } else {
                    out += c;
                }
                break;
        }
    }
    out += '"';
    return out;
}

// A regex literal: every '/' written \/.
std::string DumpRegex(const std::string& text) {
    std::string out = "/";
    for (const char c : text) {
        if (c == '/') {
            out += "\\/";
        } else {
            out += c;
        }
    }
    out += '/';
    return out;
}

// The dump spelling of a binary operator (Binary and Concat).
const char* DumpBinaryOp(ExprOp op) {
    switch (op) {
        case ExprOp::Add:      return "+";
        case ExprOp::Subtract: return "-";
        case ExprOp::Multiply: return "*";
        case ExprOp::Divide:   return "/";
        case ExprOp::Modulo:   return "%";
        case ExprOp::Power:    return "^";
        case ExprOp::Concat:   return "concat";
        case ExprOp::Less:         return "<";
        case ExprOp::LessEqual:    return "<=";
        case ExprOp::NotEqual:    return "!=";
        case ExprOp::Equal:        return "==";
        case ExprOp::Greater:     return ">";
        case ExprOp::GreaterEqual: return ">=";
        case ExprOp::Match:    return "~";
        case ExprOp::NoMatch:  return "!~";
        case ExprOp::And:      return "&&";
        case ExprOp::Or:       return "||";
        default:               return "";
    }
}

// The dump spelling of an assignment operator.
const char* DumpAssignOp(ExprOp op) {
    switch (op) {
        case ExprOp::Assign:          return "=";
        case ExprOp::AddAssign:      return "+=";
        case ExprOp::SubtractAssign: return "-=";
        case ExprOp::MultiplyAssign: return "*=";
        case ExprOp::DivideAssign:   return "/=";
        case ExprOp::ModuloAssign:   return "%=";
        case ExprOp::PowerAssign:    return "^=";
        default:                      return "";
    }
}

// The operands of a list, joined: " a b" ("" for none).
std::string DumpOperands(const std::vector<ExprPtr>& operands) {
    std::string out;
    for (const ExprPtr& operand : operands) {
        out += ' ';
        out += DumpExpr(*operand);
    }
    return out;
}

} // namespace

std::string DumpExpr(const Expr& expr) {
    switch (expr.kind) {
        case ExprKind::Number:
            return expr.text;
        case ExprKind::String:
            return DumpString(expr.text);
        case ExprKind::Regex:
            return DumpRegex(expr.text);
        case ExprKind::Variable:
            return expr.text;
        case ExprKind::Field:
            return "($ " + DumpExpr(*expr.operands[0]) + ")";
        case ExprKind::Index: {
            std::string out = expr.text + "[";
            for (size_t i = 0; i < expr.operands.size(); ++i) {
                if (i > 0) {
                    out += ", ";
                }
                out += DumpExpr(*expr.operands[i]);
            }
            out += ']';
            return out;
        }
        case ExprKind::In:
            return "(in " + expr.text + DumpOperands(expr.operands) + ")";
        case ExprKind::Unary: {
            const char* op = expr.op == ExprOp::Not ? "!" : expr.op == ExprOp::UnaryPlus ? "+" : "-";
            return "(" + std::string(op) + " " + DumpExpr(*expr.operands[0]) + ")";
        }
        case ExprKind::Binary:
            return "(" + std::string(DumpBinaryOp(expr.op)) + DumpOperands(expr.operands) + ")";
        case ExprKind::Conditional:
            return "(?:" + DumpOperands(expr.operands) + ")";
        case ExprKind::Assign:
            return "(" + std::string(DumpAssignOp(expr.op)) + DumpOperands(expr.operands) + ")";
        case ExprKind::IncDec: {
            const char* op = expr.op == ExprOp::PreIncrement    ? "pre++"
                             : expr.op == ExprOp::PreDecrement   ? "pre--"
                             : expr.op == ExprOp::PostIncrement  ? "post++"
                                                                : "post--";
            return "(" + std::string(op) + " " + DumpExpr(*expr.operands[0]) + ")";
        }
        case ExprKind::Call:
            return "(call " + expr.text + DumpOperands(expr.operands) + ")";
        case ExprKind::BuiltinCall:
            if (!expr.hasParentheses) {
                return expr.text;
            }
            return "(" + expr.text + DumpOperands(expr.operands) + ")";
        case ExprKind::Getline: {
            std::string target = expr.target ? " " + DumpExpr(*expr.target) : "";
            switch (expr.getlineForm) {
                case GetlineForm::Simple:
                    return "(getline" + target + ")";
                case GetlineForm::File:
                    return "(getline" + target + " < " + DumpExpr(*expr.operands[0]) + ")";
                case GetlineForm::Command:
                    return "(| " + DumpExpr(*expr.operands[0]) + " getline" + target + ")";
            }
            return "(getline)";
        }
    }
    return "";
}

namespace {

// The subscripts or arguments of a list, joined ", ": "s1, s2" ("" for none).
std::string DumpArgs(const std::vector<ExprPtr>& args) {
    std::string out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += DumpExpr(*args[i]);
    }
    return out;
}

// The redirect of a print/printf: " > t", " >> t" or " | t" ("" for none).
std::string DumpRedirect(const Stmt& stmt) {
    if (stmt.redirect == RedirectKind::None) {
        return "";
    }
    const char* op = stmt.redirect == RedirectKind::File   ? " > "
                     : stmt.redirect == RedirectKind::Append ? " >> "
                                                            : " | ";
    return std::string(op) + DumpExpr(*stmt.redirectTarget);
}

} // namespace

std::string DumpStmt(const Stmt& stmt) {
    switch (stmt.kind) {
        case StmtKind::Block: {
            std::string out = "{";
            for (size_t i = 0; i < stmt.statements.size(); ++i) {
                out += i == 0 ? " " : "; ";
                out += DumpStmt(*stmt.statements[i]);
            }
            out += " }";
            return out;
        }
        case StmtKind::Expression:
            return DumpExpr(*stmt.expr);
        case StmtKind::Print:
            return stmt.args.empty() ? "print" + DumpRedirect(stmt)
                                    : "print " + DumpArgs(stmt.args) + DumpRedirect(stmt);
        case StmtKind::Printf:
            return "printf " + DumpArgs(stmt.args) + DumpRedirect(stmt);
        case StmtKind::If: {
            std::string out = "if (" + DumpExpr(*stmt.expr) + ") " + DumpStmt(*stmt.body);
            if (stmt.elseBody) {
                out += " else " + DumpStmt(*stmt.elseBody);
            }
            return out;
        }
        case StmtKind::While:
            return "while (" + DumpExpr(*stmt.expr) + ") " + DumpStmt(*stmt.body);
        case StmtKind::Do:
            return "do " + DumpStmt(*stmt.body) + " while (" + DumpExpr(*stmt.expr) + ")";
        case StmtKind::For: {
            const std::string init = stmt.init ? DumpStmt(*stmt.init) : "";
            const std::string condition = stmt.expr ? DumpExpr(*stmt.expr) : "";
            const std::string update = stmt.update ? DumpStmt(*stmt.update) : "";
            return "for (" + init + "; " + condition + "; " + update + ") " +
                   DumpStmt(*stmt.body);
        }
        case StmtKind::ForIn:
            return "for (" + stmt.name + " in " + stmt.arrayName + ") " + DumpStmt(*stmt.body);
        case StmtKind::Next:
            return "next";
        case StmtKind::Nextfile:
            return "nextfile";
        case StmtKind::Break:
            return "break";
        case StmtKind::Continue:
            return "continue";
        case StmtKind::Exit:
            return stmt.expr ? "exit " + DumpExpr(*stmt.expr) : "exit";
        case StmtKind::Return:
            return stmt.expr ? "return " + DumpExpr(*stmt.expr) : "return";
        case StmtKind::Delete:
            return stmt.args.empty()
                       ? "delete " + stmt.name
                       : "delete " + stmt.name + "[" + DumpArgs(stmt.args) + "]";
    }
    return "";
}

std::string DumpProgram(const Program& program) {
    std::string out;
    for (const Item& item : program.items) {
        std::string line;
        switch (item.kind) {
            case ItemKind::Begin:
                line = "BEGIN " + DumpStmt(*item.action);
                break;
            case ItemKind::End:
                line = "END " + DumpStmt(*item.action);
                break;
            case ItemKind::Main: {
                std::string pattern;
                if (item.pattern) {
                    pattern = DumpExpr(*item.pattern);
                    if (item.rangeEnd) {
                        pattern += ", " + DumpExpr(*item.rangeEnd);
                    }
                }
                if (item.action) {
                    line = pattern.empty() ? DumpStmt(*item.action)
                                            : pattern + " " + DumpStmt(*item.action);
                } else {
                    line = pattern;
                }
                break;
            }
            case ItemKind::Function: {
                const FunctionDefinition& function = program.functions[item.functionIndex];
                line = "function " + function.name + "(";
                for (size_t i = 0; i < function.parameters.size(); ++i) {
                    if (i > 0) {
                        line += ", ";
                    }
                    line += function.parameters[i];
                }
                line += ") " + DumpStmt(*function.body);
                break;
            }
        }
        if (!out.empty()) {
            out += '\n';
        }
        out += line;
    }
    return out;
}

} // namespace Haisos::Awk