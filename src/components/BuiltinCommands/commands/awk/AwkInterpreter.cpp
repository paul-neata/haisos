#include "commands/awk/AwkInterpreter.h"

#include <cmath>

#include "BuiltinText.h"
#include "commands/awk/AwkError.h"
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

void Interpreter::ResolveExpr(const Expr& expr) {
    switch (expr.kind) {
        case ExprKind::Variable:
        case ExprKind::Index:
        case ExprKind::In:
            expr.slot = SlotOf(expr.text);
            break;
        default:
            break;
    }
    for (const ExprPtr& operand : expr.operands) {
        ResolveExpr(*operand);
    }
    if (expr.target) {
        ResolveExpr(*expr.target);
    }
}

void Interpreter::ResolveStmt(const Stmt& stmt) {
    switch (stmt.kind) {
        case StmtKind::ForIn:
            stmt.slot = SlotOf(stmt.name);
            stmt.arraySlot = SlotOf(stmt.arrayName);
            break;
        case StmtKind::Delete:
            stmt.arraySlot = SlotOf(stmt.name);
            break;
        default:
            break;
    }
    if (stmt.expr) {
        ResolveExpr(*stmt.expr);
    }
    for (const ExprPtr& argument : stmt.args) {
        ResolveExpr(*argument);
    }
    if (stmt.redirectTarget) {
        ResolveExpr(*stmt.redirectTarget);
    }
    if (stmt.init) {
        ResolveStmt(*stmt.init);
    }
    if (stmt.update) {
        ResolveStmt(*stmt.update);
    }
    if (stmt.body) {
        ResolveStmt(*stmt.body);
    }
    if (stmt.elseBody) {
        ResolveStmt(*stmt.elseBody);
    }
    for (const StmtPtr& statement : stmt.statements) {
        ResolveStmt(*statement);
    }
}

void Interpreter::Prepare() {
    // The specials first, at their fixed slots.
    for (int slot = 0; slot < kSpecialSlotCount; ++slot) {
        m_globalSlots.emplace(kSpecialNames[slot], slot);
    }
    // Every name in the program gets its slot (awk--functions will resolve
    // a function's parameters to locals instead).
    for (const Item& item : m_program->items) {
        if (item.pattern) {
            ResolveExpr(*item.pattern);
        }
        if (item.rangeEnd) {
            ResolveExpr(*item.rangeEnd);
        }
        if (item.action) {
            ResolveStmt(*item.action);
        }
    }
    for (const FunctionDefinition& function : m_program->functions) {
        ResolveStmt(*function.body);
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
    std::vector<std::string> warnings;
    const std::string decoded = DecodeAwkStringEscapes(assignment.text, warnings);
    for (const std::string& warning : warnings) {
        m_context.ErrorText(std::string(kAwkName) + ": warning: " + warning + "\n");
    }
    if (assignment.fieldSeparator) {
        // -F fs: FS as a string (gawk --posix: only the escapes decoded, -F t
        // a `t', -F '' an empty FS).
        m_globals[kSlotFS].kind = Variable::Kind::Scalar;
        m_globals[kSlotFS].scalar = Value::FromString(decoded);
        return;
    }
    const size_t equals = decoded.find('=');
    const std::string name = decoded.substr(0, equals);
    if (!IsAwkIdentifier(name)) {
        throw AwkFatal("`" + name + "' is not a legal variable name", /*withLocation=*/false);
    }
    AssignName(name, Value::FromInput(decoded.substr(equals + 1)));
}

void Interpreter::AssignName(const std::string& name, const Value& value) {
    AssignSlot(SlotOf(name), name, value);
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
    return ScalarRef(variable.slot, variable.text);
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

void Interpreter::AssignSlot(int slot, const std::string& name, const Value& value) {
    if (slot == kSlotNF) {
        // NF lives in the FieldStore, not in its (unused) variable.
        m_fields.SetNF(AwkIntegerOf(value.ToNumber()), SpecialString(kSlotOFS));
        return;
    }
    ScalarRef(slot, name) = value;
}

void Interpreter::Assign(const Expr& lvalue, const Value& value) {
    switch (lvalue.kind) {
        case ExprKind::Variable:
            AssignSlot(lvalue.slot, lvalue.text, value);
            return;
        case ExprKind::Index: {
            AwkArray& array = ArrayRef(lvalue.slot, lvalue.text);
            array.GetOrCreate(Subscript(lvalue.operands)) = value;
            return;
        }
        case ExprKind::Field:
            m_fields.SetField(AwkIntegerOf(ValueOf(*lvalue.operands[0]).ToNumber()), value,
                              SpecialString(kSlotOFS), SpecialString(kSlotCONVFMT));
            return;
        default:
            break;
    }
    throw AwkFatal("cannot assign to this expression");
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
            return ArrayRef(expr.slot, expr.text).GetOrCreate(Subscript(expr.operands));
        case ExprKind::In: {
            const Variable& variable = GlobalVariable(expr.slot);
            if (variable.kind == Variable::Kind::Scalar) {
                throw AwkFatal("attempt to use scalar `" + expr.text + "' as an array");
            }
            if (variable.kind != Variable::Kind::Array) {
                return Value::FromNumber(0);   // an untyped array: no element yet
            }
            return Value::FromNumber(variable.array->Contains(Subscript(expr.operands)) ? 1 : 0);
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
                case ExprOp::Match:
                case ExprOp::NoMatch:
                    throw AwkFatal("regular expressions are not implemented yet");
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
            // on numbers, stored back (the target is read once, as awk's).
            const Value left = ValueOf(*expr.operands[0]);
            const Value result = Value::FromNumber(CompoundArithmetic(expr.op, left.ToNumber(),
                                                                        right.ToNumber()));
            Assign(*expr.operands[0], result);
            return result;
        }
        case ExprKind::IncDec: {
            const Value old = ValueOf(*expr.operands[0]);
            const double number = old.ToNumber();
            switch (expr.op) {
                case ExprOp::PreIncrement:
                case ExprOp::PreDecrement: {
                    const Value result = Value::FromNumber(
                        expr.op == ExprOp::PreIncrement ? number + 1 : number - 1);
                    Assign(*expr.operands[0], result);
                    return result;
                }
                case ExprOp::PostIncrement:
                case ExprOp::PostDecrement: {
                    Assign(*expr.operands[0], Value::FromNumber(
                        expr.op == ExprOp::PostIncrement ? number + 1 : number - 1));
                    return old;
                }
                default: break;
            }
            break;
        }
        case ExprKind::Regex:
            throw AwkFatal("regular expressions are not implemented yet");
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
    for (const Item& item : m_program->items) {
        if (item.kind != ItemKind::Begin) {
            continue;
        }
        if (RunStatement(*item.action) == Flow::Exit) {
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
    for (size_t i = 0; i < m_program->items.size(); ++i) {
        const Item& item = m_program->items[i];
        if (item.kind != ItemKind::Main) {
            continue;
        }
        if (!MatchesPattern(item, i)) {
            continue;
        }
        if (item.action) {
            const Flow flow = RunStatement(*item.action);
            if (flow != Flow::Normal) {
                return flow;   // next, nextfile, exit end this record's items
            }
        } else {
            // A pattern without an action prints the record: `print $0' with
            // no redirection (the item has no action to carry one).
            Stmt print;
            print.kind = StmtKind::Print;
            Output(print, m_fields.Record(SpecialString(kSlotCONVFMT)) +
                              SpecialString(kSlotORS));
        }
    }
    return Flow::Normal;
}

void Interpreter::RunEndItems() {
    for (const Item& item : m_program->items) {
        if (item.kind != ItemKind::End) {
            continue;
        }
        if (RunStatement(*item.action) == Flow::Exit) {
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
        case StmtKind::Printf:
            throw AwkFatal("printf is not implemented yet");
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
            AwkArray& array = ArrayRef(stmt.arraySlot, stmt.arrayName);
            const std::vector<std::string> keys = array.Keys();   // a snapshot
            for (const std::string& key : keys) {
                ThrowIfStopped();
                if (!array.Contains(key)) {
                    continue;   // deleted by an earlier turn of this loop
                }
                AssignSlot(stmt.slot, stmt.name, Value::FromInput(key));
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
        case StmtKind::Return:
            // The parser allows return only in a function, and no function
            // runs yet (awk--functions).
            throw AwkFatal("return is not implemented yet");
        case StmtKind::Delete: {
            AwkArray& array = ArrayRef(stmt.arraySlot, stmt.name);
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
            if (SpecialString(kSlotRS).empty()) {
                throw AwkFatal("RS = \"\" (paragraph mode) is not implemented yet");
            }
            std::string record;
            const RecordReadResult result = m_reader->Next(SpecialString(kSlotRS), record);
            if (result == RecordReadResult::Record) {
                m_globals[kSlotNR].scalar =
                    Value::FromNumber(m_globals[kSlotNR].scalar.ToNumber() + 1);
                m_globals[kSlotFNR].scalar =
                    Value::FromNumber(m_globals[kSlotFNR].scalar.ToNumber() + 1);
                m_fields.SetRecord(std::move(record), SpecialString(kSlotFS), false);
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
    if (!SplitAwkFields(record, fs, fields)) {
        throw AwkFatal("regular expression field separators are not implemented yet");
    }
}

// --- the hooks awk's later tasks fill ---

Value Interpreter::CallBuiltin(const Expr& call) {
    throw AwkFatal("function `" + call.text + "' is not implemented yet");
}

Value Interpreter::CallFunction(const Expr& call) {
    throw AwkFatal("function `" + call.text + "' is not implemented yet");
}

Value Interpreter::EvaluateGetline(const Expr& expr) {
    throw AwkFatal("getline is not implemented yet");
}

// --- errors and stopping ---

void Interpreter::ReportFatal(const AwkFatal& error) {
    std::string text;
    if (error.WithLocation()) {
        const AwkSource& source =
            m_position.source >= 0 && static_cast<size_t>(m_position.source) < m_program->sources.size()
                ? m_program->sources[static_cast<size_t>(m_position.source)]
                : AwkSource{kCommandLineSourceName, ""};
        text = AwkLocationPrefix(source.name, m_position.line);
        if (m_globals[kSlotFNR].scalar.ToNumber() > 0) {
            // A record was being worked on: gawk names the file and its
            // number in it.
            text += "(FILENAME=" + m_globals[kSlotFILENAME].scalar.ToString(SpecialString(kSlotCONVFMT)) +
                    " FNR=" + m_globals[kSlotFNR].scalar.ToString(SpecialString(kSlotCONVFMT)) + ") ";
        }
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