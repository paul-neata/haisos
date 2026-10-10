#include "commands/awk/AwkInterpreter.h"

#include <cmath>

#include "BuiltinText.h"
#include "commands/awk/AwkError.h"
#include "commands/awk/AwkFormat.h"
#include "commands/awk/AwkLexer.h"
#include "interfaces/IFileIO.h"

namespace Haisos::Awk {

// The special variables, in their SpecialSlot order.
namespace {
const char* const kSpecialNames[kSpecialSlotCount] = {
    "FS", "OFS", "ORS", "RS", "SUBSEP", "CONVFMT", "OFMT",
    "NR", "FNR", "FILENAME", "RSTART", "RLENGTH",
    "ARGC", "ARGV", "ENVIRON", "NF",
};
} // namespace

Interpreter::Interpreter(BuiltinContext& context, std::shared_ptr<const Program> program,
                        const AwkInvocation& invocation)
    : m_context(context)
    , m_program(std::move(program))
    , m_invocation(invocation)
    , m_globals(kSpecialSlotCount)
    // The splitter calls back into the interpreter (SplitRecord is a hook
    // awk--records replaces); it only ever runs during Run, when the
    // object is whole.
    , m_fields([this](std::string_view record, const std::string& fs, bool paragraphMode,
                      std::vector<std::string>& fields) {
          SplitRecord(record, fs, paragraphMode, fields);
      }) {
}

// --- Prepare: before anything runs ---

int Interpreter::SlotOf(const std::string& name) {
    const auto found = m_globalSlots.find(name);
    if (found != m_globalSlots.end()) {
        return found->second;
    }
    const int slot = static_cast<int>(m_globals.size());
    m_globals.emplace_back();
    m_globalSlots.emplace(name, slot);
    return slot;
}

namespace {

// The index of |name| in a function's parameters, -1 when it is not one of
// them (or |parameters| null: a name outside a function body).
int ParameterIndex(const std::vector<std::string>* parameters, const std::string& name) {
    if (parameters == nullptr) {
        return -1;
    }
    for (size_t i = 0; i < parameters->size(); ++i) {
        if ((*parameters)[i] == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

void Interpreter::ResolveExpr(const Expr& expr, const std::vector<std::string>* parameters) {
    switch (expr.kind) {
        case ExprKind::Variable:
        case ExprKind::Index:
        case ExprKind::In: {
            const int localSlot = ParameterIndex(parameters, expr.text);
            if (localSlot >= 0) {
                expr.localSlot = localSlot;   // a parameter: no global slot
            } else {
                expr.slot = SlotOf(expr.text);
            }
            break;
        }
        case ExprKind::Call: {
            const auto found = m_functionSlots.find(expr.text);
            expr.functionIndex = found != m_functionSlots.end() ? found->second : -1;
            break;
        }
        default:
            break;
    }
    for (const ExprPtr& operand : expr.operands) {
        ResolveExpr(*operand, parameters);
    }
    if (expr.target) {
        ResolveExpr(*expr.target, parameters);
    }
}

void Interpreter::ResolveStmt(const Stmt& stmt, const std::vector<std::string>* parameters) {
    switch (stmt.kind) {
        case StmtKind::ForIn: {
            const int localSlot = ParameterIndex(parameters, stmt.name);
            if (localSlot >= 0) {
                stmt.localSlot = localSlot;
            } else {
                stmt.slot = SlotOf(stmt.name);
            }
            const int localArraySlot = ParameterIndex(parameters, stmt.arrayName);
            if (localArraySlot >= 0) {
                stmt.localArraySlot = localArraySlot;
            } else {
                stmt.arraySlot = SlotOf(stmt.arrayName);
            }
            break;
        }
        case StmtKind::Delete: {
            const int localArraySlot = ParameterIndex(parameters, stmt.name);
            if (localArraySlot >= 0) {
                stmt.localArraySlot = localArraySlot;
            } else {
                stmt.arraySlot = SlotOf(stmt.name);
            }
            break;
        }
        default:
            break;
    }
    if (stmt.expr) {
        ResolveExpr(*stmt.expr, parameters);
    }
    for (const ExprPtr& argument : stmt.args) {
        ResolveExpr(*argument, parameters);
    }
    if (stmt.redirectTarget) {
        ResolveExpr(*stmt.redirectTarget, parameters);
    }
    if (stmt.init) {
        ResolveStmt(*stmt.init, parameters);
    }
    if (stmt.update) {
        ResolveStmt(*stmt.update, parameters);
    }
    if (stmt.body) {
        ResolveStmt(*stmt.body, parameters);
    }
    if (stmt.elseBody) {
        ResolveStmt(*stmt.elseBody, parameters);
    }
    for (const StmtPtr& statement : stmt.statements) {
        ResolveStmt(*statement, parameters);
    }
}

void Interpreter::Prepare() {
    // The specials first, at their fixed slots.
    for (int slot = 0; slot < kSpecialSlotCount; ++slot) {
        m_globalSlots.emplace(kSpecialNames[slot], slot);
    }
    // Every function's name and index: a Call may precede its definition.
    for (size_t i = 0; i < m_program->functions.size(); ++i) {
        m_functionSlots.emplace(m_program->functions[i].name, static_cast<int>(i));
    }
    // Every name gets its slot: a global, or the parameter it is inside a
    // function body.
    for (const Item& item : m_program->items) {
        if (item.kind == ItemKind::Function) {
            const FunctionDefinition& function =
                m_program->functions[static_cast<size_t>(item.functionIndex)];
            ResolveStmt(*function.body, &function.parameters);
            continue;
        }
        if (item.pattern) {
            ResolveExpr(*item.pattern, nullptr);
        }
        if (item.rangeEnd) {
            ResolveExpr(*item.rangeEnd, nullptr);
        }
        if (item.action) {
            ResolveStmt(*item.action, nullptr);
        }
    }
    m_inRange.assign(m_program->items.size(), false);

    // The specials' starting values.
    m_globals[kSlotFS].kind = Variable::Kind::Scalar;
    m_globals[kSlotFS].scalar = Value::FromString(" ");
    m_globals[kSlotOFS].kind = Variable::Kind::Scalar;
    m_globals[kSlotOFS].scalar = Value::FromString(" ");
    m_globals[kSlotORS].kind = Variable::Kind::Scalar;
    m_globals[kSlotORS].scalar = Value::FromString("\n");
    m_globals[kSlotRS].kind = Variable::Kind::Scalar;
    m_globals[kSlotRS].scalar = Value::FromString("\n");
    m_globals[kSlotSUBSEP].kind = Variable::Kind::Scalar;
    m_globals[kSlotSUBSEP].scalar = Value::FromString("\034");
    m_globals[kSlotCONVFMT].kind = Variable::Kind::Scalar;
    m_globals[kSlotCONVFMT].scalar = Value::FromString("%.6g");
    m_globals[kSlotOFMT].kind = Variable::Kind::Scalar;
    m_globals[kSlotOFMT].scalar = Value::FromString("%.6g");
    m_globals[kSlotNR].kind = Variable::Kind::Scalar;
    m_globals[kSlotNR].scalar = Value::FromNumber(0);
    m_globals[kSlotFNR].kind = Variable::Kind::Scalar;
    m_globals[kSlotFNR].scalar = Value::FromNumber(0);
    m_globals[kSlotFILENAME].kind = Variable::Kind::Scalar;
    m_globals[kSlotFILENAME].scalar = Value::FromString("");
    m_globals[kSlotRSTART].kind = Variable::Kind::Scalar;
    m_globals[kSlotRSTART].scalar = Value::FromNumber(0);
    m_globals[kSlotRLENGTH].kind = Variable::Kind::Scalar;
    m_globals[kSlotRLENGTH].scalar = Value::FromNumber(-1);
    m_globals[kSlotARGC].kind = Variable::Kind::Scalar;
    m_globals[kSlotARGC].scalar = Value::FromNumber(static_cast<double>(m_invocation.operands.size()) + 1);
    m_globals[kSlotARGV].kind = Variable::Kind::Array;
    m_globals[kSlotARGV].array = std::make_shared<AwkArray>();
    m_globals[kSlotARGV].array->GetOrCreate("0") = Value::FromString(kAwkName);
    for (size_t i = 0; i < m_invocation.operands.size(); ++i) {
        m_globals[kSlotARGV].array->GetOrCreate(std::to_string(i + 1)) =
            Value::FromInput(m_invocation.operands[i]);
    }
    m_globals[kSlotENVIRON].kind = Variable::Kind::Array;
    m_globals[kSlotENVIRON].array = std::make_shared<AwkArray>();  // awk--io fills it

    // -F and -v, in the order given on the command line.
    for (const AwkPreAssignment& assignment : m_invocation.preAssignments) {
        ApplyPreAssignment(assignment);
    }
}

void Interpreter::ApplyPreAssignment(const AwkPreAssignment& assignment) {
    if (assignment.fieldSeparator) {
        // -F fs: FS as a string (gawk --posix: only the escapes decoded, -F t
        // a `t', -F '' an empty FS).
        std::vector<std::string> warnings;
        const std::string decoded = DecodeAwkStringEscapes(assignment.text, warnings);
        for (const std::string& warning : warnings) {
            m_context.ErrorText(std::string(kAwkName) + ": warning: " + warning + "\n");
        }
        m_globals[kSlotFS].kind = Variable::Kind::Scalar;
        m_globals[kSlotFS].scalar = Value::FromString(decoded);
        CheckFieldSeparator(/*withLocation=*/false);
        return;
    }
    // -v name=value: the name checked as it was written (gawk: `-v
    // 'a\x41=1'' names no variable), only the value's escapes decoded.
    const size_t equals = assignment.text.find('=');
    const std::string name = assignment.text.substr(0, equals);
    if (!IsAwkIdentifier(name)) {
        throw AwkFatal("`" + name + "' is not a legal variable name", /*withLocation=*/false);
    }
    std::vector<std::string> warnings;
    const std::string decoded = DecodeAwkStringEscapes(
        std::string_view(assignment.text).substr(equals + 1), warnings);
    for (const std::string& warning : warnings) {
        m_context.ErrorText(std::string(kAwkName) + ": warning: " + warning + "\n");
    }
    AssignName(name, Value::FromInput(decoded));
}

void Interpreter::AssignName(const std::string& name, const Value& value) {
    // -v and a name=value operand: never a statement of the program, so
    // their errors and warnings carry no location.
    AssignSlot(SlotOf(name), name, value, /*located=*/false);
}

void Interpreter::CheckFieldSeparator(bool withLocation) {
    const std::string& fs = SpecialString(kSlotFS);
    if (fs.size() < 2) {
        return;   // the empty string and one byte are never a regex
    }
    std::vector<std::string> warnings;
    std::string error;
    std::shared_ptr<const Regex> regex = m_regexCache.Get(fs, warnings, error);
    if (withLocation) {
        RegexWarnings(warnings, /*atRuntime=*/true);
        if (!regex) {
            throw AwkFatal("invalid regexp: " + error + ": /" + AwkRegexAsWritten(fs) + "/");
        }
        return;
    }
    // -F, -v and an operand: each warning once a run, no location.
    for (const std::string& message : warnings) {
        if (!m_regexWarningsGiven.insert(message).second) {
            continue;
        }
        m_context.ErrorText(std::string(kAwkName) + ": warning: " + message + "\n");
    }
    if (!regex) {
        throw AwkFatal("invalid regexp: " + error + ": /" + AwkRegexAsWritten(fs) + "/",
                       /*withLocation=*/false);
    }
}

// --- literal regexes, before anything runs ---

void Interpreter::CompileRegexesExpr(const Expr& expr) {
    if (m_regexCompileFailed) {
        return;
    }
    if (expr.kind == ExprKind::Regex) {
        m_position = expr.position;
        std::vector<std::string> warnings;
        const std::string translated = TranslateAwkRegex(expr.text, warnings);
        RegexWarnings(warnings, /*atRuntime=*/false);
        std::string error;
        expr.compiledRegex = Regex::Compile(translated, RegexOptions{RegexSyntax::Extended}, error);
        if (!expr.compiledRegex) {
            m_context.ErrorText(FormatAwkError(SourceNameAt(expr.position), expr.position.line,
                                               error + ": /" + AwkRegexAsWritten(expr.text) + "/"));
            m_regexCompileFailed = true;
            return;
        }
    }
    for (const ExprPtr& operand : expr.operands) {
        CompileRegexesExpr(*operand);
    }
    if (expr.target) {
        CompileRegexesExpr(*expr.target);
    }
}

void Interpreter::CompileRegexesStmt(const Stmt& stmt) {
    if (m_regexCompileFailed) {
        return;
    }
    switch (stmt.kind) {
        case StmtKind::For:
            // Its parts as written: init, condition, update, body.
            if (stmt.init) {
                CompileRegexesStmt(*stmt.init);
            }
            if (stmt.expr) {
                CompileRegexesExpr(*stmt.expr);
            }
            if (stmt.update) {
                CompileRegexesStmt(*stmt.update);
            }
            if (stmt.body) {
                CompileRegexesStmt(*stmt.body);
            }
            return;
        case StmtKind::Do:
            // The body before the condition, as it runs.
            if (stmt.body) {
                CompileRegexesStmt(*stmt.body);
            }
            if (stmt.expr) {
                CompileRegexesExpr(*stmt.expr);
            }
            return;
        default:
            break;
    }
    if (stmt.expr) {
        CompileRegexesExpr(*stmt.expr);
    }
    for (const ExprPtr& argument : stmt.args) {
        CompileRegexesExpr(*argument);
    }
    if (stmt.redirectTarget) {
        CompileRegexesExpr(*stmt.redirectTarget);
    }
    if (stmt.body) {
        CompileRegexesStmt(*stmt.body);
    }
    if (stmt.elseBody) {
        CompileRegexesStmt(*stmt.elseBody);
    }
    for (const StmtPtr& statement : stmt.statements) {
        CompileRegexesStmt(*statement);
    }
}

bool Interpreter::CompileLiteralRegexes() {
    for (const Item& item : m_program->items) {
        if (item.kind == ItemKind::Function) {
            CompileRegexesStmt(*m_program->functions[static_cast<size_t>(item.functionIndex)].body);
        } else {
            if (item.pattern) {
                CompileRegexesExpr(*item.pattern);
            }
            if (item.rangeEnd) {
                CompileRegexesExpr(*item.rangeEnd);
            }
            if (item.action) {
                CompileRegexesStmt(*item.action);
            }
        }
        if (m_regexCompileFailed) {
            return false;
        }
    }
    return true;
}

// --- names and values ---

Variable& Interpreter::GlobalVariable(int slot) {
    return m_globals[static_cast<size_t>(slot)];
}

Value& Interpreter::ScalarRef(int slot, const std::string& name) {
    Variable& variable = GlobalVariable(slot);
    switch (variable.kind) {
        case Variable::Kind::Array:
            throw AwkFatal("attempt to use array `" + name + "' in a scalar context");
        case Variable::Kind::Untyped:
            variable.kind = Variable::Kind::Scalar;   // reading it too types it
            break;
        case Variable::Kind::Scalar:
            break;
    }
    return variable.scalar;
}

Value& Interpreter::ScalarRef(const Expr& variable) {
    return ScalarRefOf(variable.slot, variable.localSlot, variable.text);
}

AwkArray& Interpreter::ArrayRef(int slot, const std::string& name) {
    Variable& variable = GlobalVariable(slot);
    switch (variable.kind) {
        case Variable::Kind::Scalar:
            throw AwkFatal("attempt to use scalar `" + name + "' as an array");
        case Variable::Kind::Untyped:
            variable.kind = Variable::Kind::Array;
            variable.array = std::make_shared<AwkArray>();
            break;
        case Variable::Kind::Array:
            break;
    }
    return *variable.array;
}

ActiveCall* Interpreter::CurrentCall() {
    return m_calls.empty() ? nullptr : m_calls.back().get();
}

Variable& Interpreter::VariableOf(const Expr& variable) {
    // NF never reaches here (its variable is unused); every other resolved
    // Variable expression names a global or the current call's parameter.
    return variable.localSlot >= 0
               ? CurrentCall()->locals[static_cast<size_t>(variable.localSlot)]
               : GlobalVariable(variable.slot);
}

Variable::Kind Interpreter::BoundKind(const Variable& variable) {
    // The first link of the binding chain that is not Untyped -- a global,
    // having no binding, is its own kind.
    for (const Variable* link = &variable; link != nullptr; link = link->binding) {
        if (link->kind != Variable::Kind::Untyped) {
            return link->kind;
        }
    }
    return Variable::Kind::Untyped;
}

Value& Interpreter::ScalarRef(Variable& variable, const std::string& name) {
    // The "(from ...)" name is built only on the error path: it is the
    // parameter's own passedFrom, the whole chain included.
    const auto fatal = [&variable, &name]() {
        throw AwkFatal(variable.passedFrom.empty()
                           ? "attempt to use array `" + name + "' in a scalar context"
                           : "attempt to use array `" + name + " (from " + variable.passedFrom +
                                 ")' in a scalar context");
    };
    switch (variable.kind) {
        case Variable::Kind::Array:
            fatal();
        case Variable::Kind::Untyped:
            // Typing it types every still-Untyped variable its binding chain
            // leads to (the values unchanged: an untyped `x' passed to `a'
            // and read as a scalar can no longer be an array). A variable the
            // chain reaches that has become an array since the call began is
            // gawk's fatal, as an Array parameter itself is.
            for (Variable* link = &variable; link != nullptr; link = link->binding) {
                if (link->kind == Variable::Kind::Array) {
                    fatal();
                }
                if (link->kind == Variable::Kind::Scalar) {
                    break;
                }
                link->kind = Variable::Kind::Scalar;
            }
            break;
        case Variable::Kind::Scalar:
            break;
    }
    return variable.scalar;
}

AwkArray& Interpreter::ArrayRef(Variable& variable, const std::string& name) {
    switch (variable.kind) {
        case Variable::Kind::Scalar:
            // A parameter has its own message (a Scalar argument is passed
            // by value, so only a parameter lands here).
            throw AwkFatal("attempt to use scalar parameter `" + name + "' as an array");
        case Variable::Kind::Untyped: {
            // The array lives at the chain's last variable, shared by every
            // link -- the caller's variable included, when the argument was
            // passed by name.
            std::vector<Variable*> chain;
            for (Variable* link = &variable; link != nullptr; link = link->binding) {
                if (link->kind == Variable::Kind::Scalar) {
                    throw AwkFatal("attempt to use scalar parameter `" + name + "' as an array");
                }
                chain.push_back(link);
                if (link->kind == Variable::Kind::Array) {
                    break;
                }
            }
            std::shared_ptr<AwkArray> array =
                chain.back()->kind == Variable::Kind::Array
                    ? chain.back()->array
                    : std::make_shared<AwkArray>();
            for (Variable* link : chain) {
                link->kind = Variable::Kind::Array;
                link->array = array;
            }
            return *array;
        }
        case Variable::Kind::Array:
            break;
    }
    return *variable.array;
}

Value& Interpreter::ScalarRefOf(int slot, int localSlot, const std::string& name) {
    if (localSlot < 0) {
        return ScalarRef(slot, name);
    }
    ActiveCall* call = CurrentCall();
    if (call == nullptr || localSlot >= static_cast<int>(call->locals.size())) {
        throw AwkFatal("cannot evaluate this expression");
    }
    return ScalarRef(call->locals[static_cast<size_t>(localSlot)], name);
}

AwkArray& Interpreter::ArrayRefOf(int slot, int localSlot, const std::string& name) {
    if (localSlot < 0) {
        return ArrayRef(slot, name);
    }
    ActiveCall* call = CurrentCall();
    if (call == nullptr || localSlot >= static_cast<int>(call->locals.size())) {
        throw AwkFatal("cannot evaluate this expression");
    }
    return ArrayRef(call->locals[static_cast<size_t>(localSlot)], name);
}

void Interpreter::AssignSlot(int slot, const std::string& name, const Value& value,
                             bool located) {
    if (slot == kSlotNF) {
        // NF lives in the FieldStore, not in its (unused) variable.
        m_fields.SetNF(AwkIntegerOf(value.ToNumber()), SpecialString(kSlotOFS));
        return;
    }
    ScalarRef(slot, name) = value;
    if (slot == kSlotFS) {
        // A regex FS is checked when it is assigned, not when the first
        // record is split.
        CheckFieldSeparator(located);
    }
}

void Interpreter::Assign(const Expr& lvalue, const Value& value) {
    WritePlace(PlaceOf(lvalue), value);
}

Interpreter::Place Interpreter::PlaceOf(const Expr& lvalue) {
    Place place;
    place.lvalue = &lvalue;
    switch (lvalue.kind) {
        case ExprKind::Variable:
            return place;
        case ExprKind::Index:
            place.key = Subscript(lvalue.operands);
            return place;
        case ExprKind::Field:
            place.field = AwkIntegerOf(ValueOf(*lvalue.operands[0]).ToNumber());
            return place;
        default:
            break;
    }
    throw AwkFatal("cannot assign to this expression");
}

Value Interpreter::ReadPlace(const Place& place) {
    const Expr& lvalue = *place.lvalue;
    switch (lvalue.kind) {
        case ExprKind::Index:
            return ArrayRefOf(lvalue.slot, lvalue.localSlot, lvalue.text).GetOrCreate(place.key);
        case ExprKind::Field:
            return m_fields.Field(place.field, SpecialString(kSlotCONVFMT));
        default:
            return ValueOf(lvalue);   // a Variable: no subscript to evaluate
    }
}

void Interpreter::WritePlace(const Place& place, const Value& value) {
    const Expr& lvalue = *place.lvalue;
    switch (lvalue.kind) {
        case ExprKind::Index:
            ArrayRefOf(lvalue.slot, lvalue.localSlot, lvalue.text).GetOrCreate(place.key) = value;
            return;
        case ExprKind::Field:
            if (place.field == 0) {
                // $0 = v re-splits with the FS and RS of this moment, not
                // the ones the record was read with.
                m_fields.SetRecord(value.ToString(SpecialString(kSlotCONVFMT)),
                                   SpecialString(kSlotFS),
                                   SpecialString(kSlotRS).empty());
                return;
            }
            m_fields.SetField(place.field, value, SpecialString(kSlotOFS),
                              SpecialString(kSlotCONVFMT));
            return;
        default:
            if (lvalue.localSlot >= 0) {
                ScalarRefOf(lvalue.slot, lvalue.localSlot, lvalue.text) = value;
            } else {
                AssignSlot(lvalue.slot, lvalue.text, value);
            }
            return;
    }
}

std::string Interpreter::Subscript(const std::vector<ExprPtr>& subscripts) {
    const std::string& subsep = SpecialString(kSlotSUBSEP);
    const std::string& convfmt = SpecialString(kSlotCONVFMT);
    std::string key;
    for (size_t i = 0; i < subscripts.size(); ++i) {
        if (i > 0) {
            key += subsep;
        }
        key += ValueOf(*subscripts[i]).ToString(convfmt);
    }
    return key;
}

const std::string& Interpreter::SpecialString(int slot) {
    // A number assigned to one of these becomes its string here, through
    // CONVFMT (CONVFMT and OFMT through the default "%.6g", never through
    // themselves). Each slot has its own entry, so two of them handed to one
    // call (`SetField` takes OFS and CONVFMT) never alias.
    const std::string& convfmt = slot == kSlotCONVFMT || slot == kSlotOFMT
                                     ? std::string("%.6g")
                                     : SpecialString(kSlotCONVFMT);
    m_specialStrings[slot] = m_globals[static_cast<size_t>(slot)].scalar.ToString(convfmt);
    return m_specialStrings[slot];
}

// --- evaluation ---

namespace {

// The five comparison operators over a CompareValues result (kAwkUnordered
// checked by the caller). -> 1/0, as awk prints a truth value.
int ComparisonOf(int result, ExprOp op) {
    switch (op) {
        case ExprOp::Less:          return result < 0;
        case ExprOp::LessEqual:     return result <= 0;
        case ExprOp::Equal:         return result == 0;
        case ExprOp::NotEqual:      return result != 0;
        case ExprOp::Greater:       return result > 0;
        case ExprOp::GreaterEqual:  return result >= 0;
        default: break;
    }
    return 0;
}

// The arithmetic of the compound assignments, with their division messages.
double CompoundArithmetic(ExprOp op, double left, double right) {
    switch (op) {
        case ExprOp::AddAssign:    return left + right;
        case ExprOp::SubtractAssign: return left - right;
        case ExprOp::MultiplyAssign: return left * right;
        case ExprOp::DivideAssign:
            if (right == 0) {
                throw AwkFatal("division by zero attempted in `/='");
            }
            return left / right;
        case ExprOp::ModuloAssign:
            if (right == 0) {
                throw AwkFatal("division by zero attempted in `%='");
            }
            return std::fmod(left, right);
        case ExprOp::PowerAssign:  return std::pow(left, right);
        default: break;
    }
    return 0;
}

} // namespace

Value Interpreter::ValueOf(const Expr& expr) {
    switch (expr.kind) {
        case ExprKind::Number:
            return Value::FromNumber(expr.number);
        case ExprKind::String:
            return Value::FromString(expr.text);
        case ExprKind::Variable:
            if (expr.slot == kSlotNF) {
                return Value::FromNumber(static_cast<double>(m_fields.NF()));
            }
            return ScalarRef(expr);
        case ExprKind::Field:
            return m_fields.Field(AwkIntegerOf(ValueOf(*expr.operands[0]).ToNumber()),
                                  SpecialString(kSlotCONVFMT));
        case ExprKind::Index:
            return ArrayRefOf(expr.slot, expr.localSlot, expr.text)
                .GetOrCreate(Subscript(expr.operands));
        case ExprKind::In: {
            // `k in x' types an untyped x as an array, as using it any other
            // way types it a scalar.
            AwkArray& array = ArrayRefOf(expr.slot, expr.localSlot, expr.text);
            return Value::FromNumber(array.Contains(Subscript(expr.operands)) ? 1 : 0);
        }
        case ExprKind::Unary: {
            const Value operand = ValueOf(*expr.operands[0]);
            switch (expr.op) {
                case ExprOp::Negate:     return Value::FromNumber(-operand.ToNumber());
                case ExprOp::UnaryPlus:  return Value::FromNumber(operand.ToNumber());
                case ExprOp::Not:       return Value::FromNumber(operand.ToBoolean() ? 0 : 1);
                default: break;
            }
            break;
        }
        case ExprKind::Binary: {
            switch (expr.op) {
                case ExprOp::And:
                    if (!ValueOf(*expr.operands[0]).ToBoolean()) {
                        return Value::FromNumber(0);
                    }
                    return Value::FromNumber(ValueOf(*expr.operands[1]).ToBoolean() ? 1 : 0);
                case ExprOp::Or:
                    if (ValueOf(*expr.operands[0]).ToBoolean()) {
                        return Value::FromNumber(1);
                    }
                    return Value::FromNumber(ValueOf(*expr.operands[1]).ToBoolean() ? 1 : 0);
                case ExprOp::Match:
                case ExprOp::NoMatch: {
                    // The left operand a string, the right a regex -- a
                    // dynamic one evaluated as a value, a literal taken whole.
                    const std::string left =
                        ValueOf(*expr.operands[0]).ToString(SpecialString(kSlotCONVFMT));
                    const bool matched = MatchRegex(*expr.operands[1], left);
                    return Value::FromNumber(matched == (expr.op == ExprOp::Match) ? 1 : 0);
                }
                default: break;
            }
            const Value left = ValueOf(*expr.operands[0]);
            const Value right = ValueOf(*expr.operands[1]);
            switch (expr.op) {
                case ExprOp::Add:          return Value::FromNumber(left.ToNumber() + right.ToNumber());
                case ExprOp::Subtract:     return Value::FromNumber(left.ToNumber() - right.ToNumber());
                case ExprOp::Multiply:     return Value::FromNumber(left.ToNumber() * right.ToNumber());
                case ExprOp::Divide: {
                    const double divisor = right.ToNumber();
                    if (divisor == 0) {
                        throw AwkFatal("division by zero attempted");
                    }
                    return Value::FromNumber(left.ToNumber() / divisor);
                }
                case ExprOp::Modulo: {
                    const double divisor = right.ToNumber();
                    if (divisor == 0) {
                        throw AwkFatal("division by zero attempted in `%'");
                    }
                    return Value::FromNumber(std::fmod(left.ToNumber(), divisor));
                }
                case ExprOp::Power:        return Value::FromNumber(std::pow(left.ToNumber(), right.ToNumber()));
                case ExprOp::Concat:
                    return Value::FromString(left.ToString(SpecialString(kSlotCONVFMT)) +
                                              right.ToString(SpecialString(kSlotCONVFMT)));
                default: {
                    const int result = CompareValues(left, right, SpecialString(kSlotCONVFMT));
                    if (result == kAwkUnordered) {
                        // A NaN on either side: only != is true.
                        return Value::FromNumber(expr.op == ExprOp::NotEqual ? 1 : 0);
                    }
                    return Value::FromNumber(ComparisonOf(result, expr.op));
                }
            }
        }
        case ExprKind::Conditional:
            if (ValueOf(*expr.operands[0]).ToBoolean()) {
                return ValueOf(*expr.operands[1]);
            }
            return ValueOf(*expr.operands[2]);
        case ExprKind::Assign: {
            const Value right = ValueOf(*expr.operands[1]);
            if (expr.op == ExprOp::Assign) {
                Assign(*expr.operands[0], right);
                return right;
            }
            // A compound assignment: the value of the target, the arithmetic
            // on numbers, stored back -- the target's subscript or field
            // index evaluated once (gawk: a[i++] += 1 adds 1 to i once).
            const Place place = PlaceOf(*expr.operands[0]);
            const Value left = ReadPlace(place);
            const Value result = Value::FromNumber(CompoundArithmetic(expr.op, left.ToNumber(),
                                                                        right.ToNumber()));
            WritePlace(place, result);
            return result;
        }
        case ExprKind::IncDec: {
            const Place place = PlaceOf(*expr.operands[0]);
            const double number = ReadPlace(place).ToNumber();
            switch (expr.op) {
                case ExprOp::PreIncrement:
                case ExprOp::PreDecrement: {
                    const Value result = Value::FromNumber(
                        expr.op == ExprOp::PreIncrement ? number + 1 : number - 1);
                    WritePlace(place, result);
                    return result;
                }
                case ExprOp::PostIncrement:
                case ExprOp::PostDecrement: {
                    WritePlace(place, Value::FromNumber(
                        expr.op == ExprOp::PostIncrement ? number + 1 : number - 1));
                    return Value::FromNumber(number);   // the old number, never its text
                }
                default: break;
            }
            break;
        }
        case ExprKind::Regex:
            // A regex on its own, anywhere in an expression: $0 matched
            // against it, whether parenthesized or not (a parenthesized one
            // only stops `~' from taking it as itself).
            return Value::FromNumber(
                MatchRegexLiteral(expr, m_fields.Record(SpecialString(kSlotCONVFMT))) ? 1 : 0);
        case ExprKind::Call:
            return CallFunction(expr);
        case ExprKind::BuiltinCall:
            return CallBuiltin(expr);
        case ExprKind::Getline:
            return EvaluateGetline(expr);
    }
    throw AwkFatal("cannot evaluate this expression");
}

// --- Run ---

int Interpreter::Run() {
    try {
        Prepare();
        if (!CompileLiteralRegexes()) {
            return 1;   // the error reported, nothing runs
        }
        RunBeginItems();
        RunMainLoop();
        RunEndItems();
    } catch (const AwkFatal& error) {
        ReportFatal(error);
        return 2;
    } catch (const Stopped&) {
        return 143;
    }
    return static_cast<int>(m_exitCode & 0xFF);
}

void Interpreter::RunBeginItems() {
    m_currentItemKind = ItemKind::Begin;
    for (const Item& item : m_program->items) {
        if (item.kind != ItemKind::Begin) {
            continue;
        }
        // Only exit can arrive here, run or unwound: CallFunction makes next
        // and nextfile out of a BEGIN rule a fatal, and break/continue never
        // leave a function body.
        Flow flow = Flow::Normal;
        try {
            flow = RunStatement(*item.action);
        } catch (const FlowUnwind& unwind) {
            flow = unwind.flow;
        }
        if (flow == Flow::Exit) {
            // exit in BEGIN: the input is skipped, END still runs.
            m_exitFromBegin = true;
            return;
        }
    }
}

void Interpreter::RunMainLoop() {
    if (m_exitFromBegin) {
        return;
    }
    // Without a Main item there is nothing to run over the records; without
    // an END item nothing after them either -- so no input is read at all
    // (gawk --posix: a BEGIN-only program never touches its input).
    bool hasMain = false;
    bool hasEnd = false;
    for (const Item& item : m_program->items) {
        hasMain = hasMain || item.kind == ItemKind::Main;
        hasEnd = hasEnd || item.kind == ItemKind::End;
    }
    if (!hasMain && !hasEnd) {
        return;
    }
    while (NextMainRecord()) {
        ThrowIfStopped();
        const Flow flow = RunMainItems();
        if (flow == Flow::Exit) {
            return;
        }
        if (flow == Flow::NextFile) {
            // The current input is done with: the next record comes from the
            // next operand (or whatever ARGV says by then).
            m_reader.reset();
        }
    }
}

Flow Interpreter::RunMainItems() {
    m_currentItemKind = ItemKind::Main;
    for (size_t i = 0; i < m_program->items.size(); ++i) {
        const Item& item = m_program->items[i];
        if (item.kind != ItemKind::Main) {
            continue;
        }
        Flow flow = Flow::Normal;
        try {
            // A pattern may hold a call whose next/nextfile/exit unwinds
            // here, as the action's would.
            if (!MatchesPattern(item, i)) {
                continue;
            }
            if (item.action) {
                flow = RunStatement(*item.action);
            } else {
                // A pattern without an action prints the record: `print $0'
                // with no redirection (the item has no action to carry one).
                Stmt print;
                print.kind = StmtKind::Print;
                Output(print, m_fields.Record(SpecialString(kSlotCONVFMT)) +
                                  SpecialString(kSlotORS));
            }
        } catch (const FlowUnwind& unwind) {
            flow = unwind.flow;
        }
        if (flow != Flow::Normal) {
            return flow;   // next, nextfile, exit end this record's items
        }
    }
    return Flow::Normal;
}

void Interpreter::RunEndItems() {
    m_currentItemKind = ItemKind::End;
    for (const Item& item : m_program->items) {
        if (item.kind != ItemKind::End) {
            continue;
        }
        // Only exit can arrive here, run or unwound (RunBeginItems' comment).
        Flow flow = Flow::Normal;
        try {
            flow = RunStatement(*item.action);
        } catch (const FlowUnwind& unwind) {
            flow = unwind.flow;
        }
        if (flow == Flow::Exit) {
            return;   // exit in END stops at once
        }
    }
}

// --- statements ---

Flow Interpreter::RunStatement(const Stmt& stmt) {
    m_position = stmt.position;
    switch (stmt.kind) {
        case StmtKind::Expression:
            ValueOf(*stmt.expr);
            return Flow::Normal;
        case StmtKind::Print: {
            std::string text;
            if (stmt.args.empty()) {
                text = m_fields.Record(SpecialString(kSlotCONVFMT));
            } else {
                const std::string& ofs = SpecialString(kSlotOFS);
                const std::string& ofmt = SpecialString(kSlotOFMT);
                for (size_t i = 0; i < stmt.args.size(); ++i) {
                    if (i > 0) {
                        text += ofs;
                    }
                    text += ValueOf(*stmt.args[i]).ToString(ofmt);
                }
            }
            text += SpecialString(kSlotORS);
            Output(stmt, text);
            return Flow::Normal;
        }
        case StmtKind::Printf: {
            // A bare printf prints nothing (and no ORS, as gawk's).
            if (stmt.args.empty()) {
                return Flow::Normal;
            }
            std::vector<Value> arguments;
            arguments.reserve(stmt.args.size());
            for (const ExprPtr& argument : stmt.args) {
                arguments.push_back(ValueOf(*argument));
            }
            const std::string& convfmt = SpecialString(kSlotCONVFMT);
            const std::string format = arguments[0].ToString(convfmt);
            arguments.erase(arguments.begin());
            // The whole text is built before anything is written, so a format
            // that runs out fails its printf whole.
            Output(stmt, FormatAwkPrintf(format, arguments, convfmt));
            return Flow::Normal;
        }
        case StmtKind::If:
            if (ValueOf(*stmt.expr).ToBoolean()) {
                return RunStatement(*stmt.body);
            }
            if (stmt.elseBody) {
                return RunStatement(*stmt.elseBody);
            }
            return Flow::Normal;
        case StmtKind::While:
            while (true) {
                ThrowIfStopped();
                if (!ValueOf(*stmt.expr).ToBoolean()) {
                    break;
                }
                const Flow flow = RunStatement(*stmt.body);
                if (flow == Flow::Break) {
                    break;
                }
                if (flow != Flow::Continue && flow != Flow::Normal) {
                    return flow;   // next, nextfile, exit (a loop keeps none)
                }
            }
            return Flow::Normal;
        case StmtKind::Do:
            while (true) {
                ThrowIfStopped();
                const Flow flow = RunStatement(*stmt.body);
                if (flow == Flow::Break) {
                    break;
                }
                if (flow != Flow::Continue && flow != Flow::Normal) {
                    return flow;
                }
                if (!ValueOf(*stmt.expr).ToBoolean()) {
                    break;
                }
            }
            return Flow::Normal;
        case StmtKind::For:
            if (stmt.init) {
                RunStatement(*stmt.init);
            }
            while (true) {
                ThrowIfStopped();
                if (stmt.expr && !ValueOf(*stmt.expr).ToBoolean()) {
                    break;
                }
                const Flow flow = RunStatement(*stmt.body);
                if (flow == Flow::Break) {
                    break;
                }
                // Continue and Normal both go on to the update; anything else
                // leaves the loop (and the record, or the program).
                if (flow != Flow::Continue && flow != Flow::Normal) {
                    return flow;
                }
                if (stmt.update) {
                    RunStatement(*stmt.update);
                }
            }
            return Flow::Normal;
        case StmtKind::ForIn: {
            AwkArray& array = ArrayRefOf(stmt.arraySlot, stmt.localArraySlot, stmt.arrayName);
            const std::vector<std::string> keys = array.Keys();   // a snapshot
            for (const std::string& key : keys) {
                ThrowIfStopped();
                if (!array.Contains(key)) {
                    continue;   // deleted by an earlier turn of this loop
                }
                if (stmt.localSlot >= 0) {
                    ScalarRefOf(stmt.slot, stmt.localSlot, stmt.name) =
                        Value::FromInput(key);
                } else {
                    AssignSlot(stmt.slot, stmt.name, Value::FromInput(key));
                }
                const Flow flow = RunStatement(*stmt.body);
                if (flow == Flow::Break) {
                    break;
                }
                if (flow != Flow::Continue && flow != Flow::Normal) {
                    return flow;
                }
            }
            return Flow::Normal;
        }
        case StmtKind::Block:
            for (const StmtPtr& statement : stmt.statements) {
                const Flow flow = RunStatement(*statement);
                if (flow != Flow::Normal) {
                    return flow;   // the first non-normal flow ends the block
                }
            }
            return Flow::Normal;
        case StmtKind::Next:
            return Flow::Next;
        case StmtKind::Nextfile:
            return Flow::NextFile;
        case StmtKind::Break:
            return Flow::Break;
        case StmtKind::Continue:
            return Flow::Continue;
        case StmtKind::Exit:
            if (stmt.expr) {
                m_exitCode = AwkIntegerOf(ValueOf(*stmt.expr).ToNumber());
            }
            return Flow::Exit;
        case StmtKind::Return: {
            // The parser allows return only in a function body, so a call is
            // always active here.
            ActiveCall* call = CurrentCall();
            if (call != nullptr) {
                if (stmt.expr) {
                    call->returnValue = ValueOf(*stmt.expr);
                }
            }
            return Flow::Return;
        }
        case StmtKind::Delete: {
            AwkArray& array = ArrayRefOf(stmt.arraySlot, stmt.localArraySlot, stmt.name);
            if (stmt.args.empty()) {
                array.Clear();
            } else {
                array.Remove(Subscript(stmt.args));
            }
            return Flow::Normal;
        }
    }
    return Flow::Normal;
}

// --- items and input ---

bool Interpreter::MatchesPattern(const Item& item, size_t itemIndex) {
    if (!item.pattern) {
        return true;   // an action alone matches every record
    }
    if (!item.rangeEnd) {
        m_position = item.pattern->position;
        return ValueOf(*item.pattern).ToBoolean();
    }
    // A range p1, p2: per item a flag, false until p1 matches a record
    // (vector<bool> gives no reference to hold, so each turn goes through
    // the flag's own slot).
    if (!m_inRange[itemIndex]) {
        m_position = item.pattern->position;
        if (!ValueOf(*item.pattern).ToBoolean()) {
            return false;
        }
        m_position = item.rangeEnd->position;
        m_inRange[itemIndex] = !ValueOf(*item.rangeEnd).ToBoolean();
        return true;
    }
    m_position = item.rangeEnd->position;
    if (ValueOf(*item.rangeEnd).ToBoolean()) {
        m_inRange[itemIndex] = false;
    }
    return true;
}

bool Interpreter::NextMainRecord() {
    while (true) {
        if (m_reader) {
            std::string record;
            const RecordReadResult result = m_reader->Next(SpecialString(kSlotRS), record);
            if (result == RecordReadResult::Record) {
                m_globals[kSlotNR].scalar =
                    Value::FromNumber(m_globals[kSlotNR].scalar.ToNumber() + 1);
                m_globals[kSlotFNR].scalar =
                    Value::FromNumber(m_globals[kSlotFNR].scalar.ToNumber() + 1);
                m_fields.SetRecord(std::move(record), SpecialString(kSlotFS),
                                   SpecialString(kSlotRS).empty());
                return true;
            }
            if (result == RecordReadResult::Stopped) {
                throw Stopped{};
            }
            if (result == RecordReadResult::Error) {
                throw AwkFatal("error reading input file `" +
                               m_globals[kSlotFILENAME].scalar.ToString(SpecialString(kSlotCONVFMT)) +
                               "': Input/output error", /*withLocation=*/false);
            }
            m_reader.reset();   // End: this input is done with
        }
        if (!OpenNextInput()) {
            return false;
        }
    }
}

bool Interpreter::OpenNextInput() {
    while (true) {
        const intmax_t argc = AwkIntegerOf(m_globals[kSlotARGC].scalar.ToNumber());
        if (m_argvIndex >= argc) {
            break;
        }
        const intmax_t index = m_argvIndex;
        ++m_argvIndex;
        Value* element = ArrayRef(kSlotARGV, "ARGV").Find(std::to_string(index));
        if (!element || element->ToString(SpecialString(kSlotCONVFMT)).empty()) {
            continue;   // absent or empty: skipped, as ARGV's holes are
        }
        const std::string text = element->ToString(SpecialString(kSlotCONVFMT));
        const size_t equals = text.find('=');
        if (equals != std::string::npos && IsAwkIdentifier(text.substr(0, equals))) {
            // name=value: an assignment made now (escapes decoded, from
            // input), a special variable included.
            std::vector<std::string> warnings;
            const std::string decoded = DecodeAwkStringEscapes(
                std::string_view(text).substr(equals + 1), warnings);
            for (const std::string& warning : warnings) {
                m_context.ErrorText(std::string(kAwkName) + ": warning: " + warning + "\n");
            }
            AssignName(text.substr(0, equals), Value::FromInput(decoded));
            continue;
        }
        InputOpenFailure failure = InputOpenFailure::None;
        std::shared_ptr<IFileDescriptor> input = OpenInputOperand(m_context, text, failure);
        if (!input) {
            throw AwkFatal("cannot open file `" + text + "' for reading: " +
                               OpenFailureText(failure), /*withLocation=*/false);
        }
        m_globals[kSlotFILENAME].kind = Variable::Kind::Scalar;
        m_globals[kSlotFILENAME].scalar = Value::FromString(text);
        m_globals[kSlotFNR].kind = Variable::Kind::Scalar;
        m_globals[kSlotFNR].scalar = Value::FromNumber(0);
        m_reader = std::make_shared<RecordReader>(m_context, std::move(input));
        m_openedInput = true;
        return true;
    }
    // Every operand is used up: with no input opened at all, the standard
    // input is read once (gawk --posix names it "-").
    if (m_openedInput) {
        return false;
    }
    InputOpenFailure failure = InputOpenFailure::None;
    std::shared_ptr<IFileDescriptor> input = OpenInputOperand(m_context, "-", failure);
    if (!input) {
        throw AwkFatal(std::string("cannot open file `-' for reading: ") + OpenFailureText(failure),
                       /*withLocation=*/false);
    }
    m_globals[kSlotFILENAME].kind = Variable::Kind::Scalar;
    m_globals[kSlotFILENAME].scalar = Value::FromString("-");
    m_globals[kSlotFNR].kind = Variable::Kind::Scalar;
    m_globals[kSlotFNR].scalar = Value::FromNumber(0);
    m_reader = std::make_shared<RecordReader>(m_context, std::move(input));
    m_openedInput = true;
    return true;
}

void Interpreter::Output(const Stmt& print, const std::string& text) {
    if (print.redirect != RedirectKind::None) {
        throw AwkFatal("output redirection is not implemented yet");
    }
    m_context.Out(text);
}

void Interpreter::SplitRecord(std::string_view record, const std::string& fs, bool paragraphMode,
                              std::vector<std::string>& fields) {
    if (fs.size() >= 2) {
        // FS of two or more bytes: an ERE, the newline not also a separator
        // in paragraph mode (gawk leaves the cutting to the regex).
        std::vector<std::string> warnings;
        std::string error;
        std::shared_ptr<const Regex> regex = m_regexCache.Get(fs, warnings, error);
        RegexWarnings(warnings, /*atRuntime=*/true);
        if (!regex) {
            throw AwkFatal("invalid regexp: " + error + ": /" + AwkRegexAsWritten(fs) + "/");
        }
        SplitByRegex(record, *regex, fields);
        return;
    }
    if (paragraphMode) {
        // RS = "": the record's lines are the separator's pieces, each split
        // by FS on its own -- with FS the blank, the lines' words; with FS
        // the empty string, each line whole.
        fields.clear();
        size_t start = 0;
        for (;;) {
            const size_t newline = record.find('\n', start);
            const size_t end = newline == std::string_view::npos ? record.size() : newline;
            std::vector<std::string> pieceFields;
            SplitAwkFields(record.substr(start, end - start), fs, pieceFields);
            fields.insert(fields.end(), pieceFields.begin(), pieceFields.end());
            if (newline == std::string_view::npos) {
                return;
            }
            start = newline + 1;
        }
    }
    SplitAwkFields(record, fs, fields);
}

// --- regexes ---

bool Interpreter::MatchRegexLiteral(const Expr& regex, const std::string& text) {
    RegexMatch match;
    return regex.compiledRegex->Search(text, 0, match);
}

std::shared_ptr<const Regex> Interpreter::RegexOperand(const Expr& operand) {
    if (operand.kind == ExprKind::Regex && !operand.parenthesized) {
        return operand.compiledRegex;
    }
    // A dynamic regex: the operand's value a string, compiled once. A
    // parenthesized regex literal comes here too: gawk takes it as the
    // value of `($0 ~ /re/)', a number whose text is then the regex.
    const std::string value = ValueOf(operand).ToString(SpecialString(kSlotCONVFMT));
    std::vector<std::string> warnings;
    std::string error;
    std::shared_ptr<const Regex> compiled = m_regexCache.Get(value, warnings, error);
    RegexWarnings(warnings, /*atRuntime=*/true);
    if (!compiled) {
        throw AwkFatal("invalid regexp: " + error + ": /" + AwkRegexAsWritten(value) + "/");
    }
    return compiled;
}

bool Interpreter::MatchRegex(const Expr& regex, const std::string& text) {
    std::shared_ptr<const Regex> compiled = RegexOperand(regex);
    RegexMatch match;
    return compiled->Search(text, 0, match);
}

void Interpreter::RegexWarnings(const std::vector<std::string>& messages, bool atRuntime) {
    for (const std::string& message : messages) {
        if (!m_regexWarningsGiven.insert(message).second) {
            continue;   // gawk reports each escape warning once a run
        }
        if (atRuntime) {
            RuntimeWarning(message);
        } else {
            m_context.ErrorText(FormatAwkWarning(AwkWarning{SourceNameAt(m_position),
                                                            m_position.line, message}));
        }
    }
}

void Interpreter::RuntimeWarning(const std::string& message) {
    m_context.ErrorText(LocatedPrefix() + "warning: " + message + "\n");
}

// --- the hooks awk's later tasks fill ---

Value Interpreter::CallFunction(const Expr& call) {
    if (call.functionIndex < 0) {
        // Only when the call runs: a definition may come later in the
        // program, and a call never made is fine.
        throw AwkFatal("function `" + call.text + "' not defined");
    }
    const FunctionDefinition& function =
        m_program->functions[static_cast<size_t>(call.functionIndex)];
    if (call.operands.size() > function.parameters.size()) {
        RuntimeWarning("function `" + call.text + "' called with more arguments than declared");
    }
    if (m_calls.size() >= kAwkMaxCallDepth) {
        throw AwkFatal("function call nesting too deep (more than " +
                       std::to_string(kAwkMaxCallDepth) + " calls)");
    }
    auto active = std::make_unique<ActiveCall>();
    active->function = &function;
    active->locals.resize(function.parameters.size());
    for (size_t i = 0; i < call.operands.size(); ++i) {
        const Expr& argument = *call.operands[i];
        if (i >= active->locals.size()) {
            ValueOf(argument);   // an extra argument: evaluated, then dropped
            continue;
        }
        Variable& parameter = active->locals[i];
        // A bare variable is passed by name when it is an array or untyped;
        // the special variables (NF's own variable is unused) are always by
        // value, as any other expression is.
        const bool bareVariable = argument.kind == ExprKind::Variable
                                  && !argument.parenthesized && argument.slot != kSlotNF;
        if (!bareVariable) {
            parameter.kind = Variable::Kind::Scalar;
            parameter.scalar = ValueOf(argument);
            continue;
        }
        Variable& passed = VariableOf(argument);
        // Where the argument came from, for the "(from ...)" error texts:
        // the argument's name, and after it its own chain when it is itself
        // a parameter passed by name ("a", or "a, from x").
        const std::string passedFrom =
            passed.passedFrom.empty() ? argument.text
                                      : argument.text + ", from " + passed.passedFrom;
        switch (passed.kind) {
            case Variable::Kind::Array:
                parameter.kind = Variable::Kind::Array;
                parameter.array = passed.array;
                parameter.passedFrom = passedFrom;
                break;
            case Variable::Kind::Untyped:
                // The callee may make it an array (or a scalar) in the
                // caller, through the binding chain.
                parameter.kind = Variable::Kind::Untyped;
                parameter.binding = &passed;
                parameter.passedFrom = passedFrom;
                break;
            case Variable::Kind::Scalar:
                parameter.kind = Variable::Kind::Scalar;
                parameter.scalar = passed.scalar;
                break;
        }
    }
    const SourcePosition position = m_position;
    m_calls.push_back(std::move(active));
    Flow flow;
    try {
        flow = RunStatement(*function.body);
    } catch (...) {
        // A fatal, a stop or a nested flow: this call is unwound either way.
        m_calls.pop_back();
        throw;
    }
    Value result = std::move(m_calls.back()->returnValue);
    m_calls.pop_back();
    m_position = position;
    switch (flow) {
        case Flow::Normal:
        case Flow::Return:
            return result;
        case Flow::Next:
        case Flow::NextFile:
            if (m_currentItemKind != ItemKind::Main) {
                const char* statement = flow == Flow::Next ? "next" : "nextfile";
                throw AwkFatal(std::string("`") + statement + "' cannot be called from a `" +
                               (m_currentItemKind == ItemKind::Begin ? "BEGIN" : "END") +
                               "' rule");
            }
            throw FlowUnwind{flow};
        case Flow::Exit:
            throw FlowUnwind{Flow::Exit};   // m_exitCode is already set
        default:
            break;   // break/continue: the parser refuses them in a function
    }
    return result;
}

Value Interpreter::EvaluateGetline(const Expr& expr) {
    throw AwkFatal("getline is not implemented yet");
}

// --- errors and stopping ---

std::string Interpreter::SourceNameAt(const SourcePosition& position) {
    return position.source >= 0 && static_cast<size_t>(position.source) < m_program->sources.size()
               ? m_program->sources[static_cast<size_t>(position.source)].name
               : std::string(kCommandLineSourceName);
}

std::string Interpreter::LocatedPrefix() {
    std::string text = AwkLocationPrefix(SourceNameAt(m_position), m_position.line);
    if (m_globals[kSlotFNR].scalar.ToNumber() > 0) {
        // A record was being worked on: gawk names the file and its number
        // in it.
        text += "(FILENAME=" + m_globals[kSlotFILENAME].scalar.ToString(SpecialString(kSlotCONVFMT)) +
                " FNR=" + m_globals[kSlotFNR].scalar.ToString(SpecialString(kSlotCONVFMT)) + ") ";
    }
    return text;
}

void Interpreter::ReportFatal(const AwkFatal& error) {
    std::string text;
    if (error.WithLocation()) {
        text = LocatedPrefix();
    } else {
        text = std::string(kAwkName) + ": ";
    }
    text += "fatal: ";
    text += error.what();
    text += "\n";
    m_context.ErrorText(text);
}

void Interpreter::ThrowIfStopped() {
    if (m_context.StopRequested()) {
        throw Stopped{};
    }
}

} // namespace Haisos::Awk