#pragma once
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "BuiltinCommand.h"
#include "commands/awk/AwkAst.h"
#include "commands/awk/AwkFields.h"
#include "commands/awk/AwkInput.h"
#include "commands/awk/AwkInvocation.h"
#include "commands/awk/AwkRegex.h"
#include "commands/awk/AwkValue.h"

namespace Haisos::Awk {

// How deep user function calls may nest. gawk has no fixed limit; a number
// this side of exhausting the machine keeps a runaway recursion fatal
// instead of crashing.
inline constexpr size_t kAwkMaxCallDepth = 200;

// A variable's storage. Untyped until first used: as a scalar (reading it
// too) it becomes Scalar, as an array Array; the other use is then an
// AwkFatal. The array is shared so awk--functions can pass it by reference.
struct Variable {
    enum class Kind { Untyped, Scalar, Array };
    Kind kind = Kind::Untyped;
    Value scalar;
    std::shared_ptr<AwkArray> array;
    // The caller's variable an untyped parameter was passed by name: using
    // the parameter types and (for arrays) reaches the binding's own chain
    // end. Raw: a binding lives in the caller's frame, which outlives every
    // call made from it.
    Variable* binding = nullptr;
    // The name the argument was passed from, for gawk's "(from x)" error
    // texts.
    std::string passedFrom;
};

// The special variables, at these fixed global slots (registered first, in
// this order); NF's variable is unused -- NF lives in the FieldStore.
enum SpecialSlot : int {
    kSlotFS, kSlotOFS, kSlotORS, kSlotRS, kSlotSUBSEP, kSlotCONVFMT, kSlotOFMT,
    kSlotNR, kSlotFNR, kSlotFILENAME, kSlotRSTART, kSlotRLENGTH,
    kSlotARGC, kSlotARGV, kSlotENVIRON, kSlotNF, kSpecialSlotCount,
};

// What a statement tells the statements around it.
enum class Flow { Normal, Break, Continue, Next, NextFile, Exit, Return };

// One active user function call: the definition being run, its frame of
// parameters and extra locals, and the value `return` left (Uninitialized
// without one).
struct ActiveCall {
    const FunctionDefinition* function = nullptr;
    std::vector<Variable> locals;
    Value returnValue;
};

// next/nextfile/exit out of a function body: caught by the item loops the
// flow belongs to (the call is already unwound when it is thrown).
struct FlowUnwind {
    Flow flow;
};

// Runs a parsed program: the BEGIN items, the main items over the input's
// records, the END items (see "Running" in the awk CLAUDE.md). A plain class
// (no interface); one per run of the command. Its shape is fixed: awk's
// later tasks (records, functions, io) fill in the hooks named below.
class Interpreter {
public:
    Interpreter(BuiltinContext& context, std::shared_ptr<const Program> program,
                const AwkInvocation& invocation);
    // Runs the program; returns awk's exit status: the exit code (0 by
    // default) taken modulo 256, 2 after a fatal error (reported), 143 when
    // stopped.
    int Run();

private:
    // A stop asked for mid-run: unwinds to Run. Private: nothing outside
    // throws it, and Run is the only catcher.
    struct Stopped {};

    // --- before anything runs ---
    void Prepare();                          // resolve names to slots; set the special variables; -F/-v
    int SlotOf(const std::string& name);     // the global slot of a name, created if new
    // Gives every name its slot: a global's, or the parameter it is inside
    // a function body (parameters null: a global, whatever it is). A Call's
    // definition index is resolved too, calls before their definitions
    // included.
    void ResolveExpr(const Expr& expr, const std::vector<std::string>* parameters);
    void ResolveStmt(const Stmt& stmt, const std::vector<std::string>* parameters);
    void ApplyPreAssignment(const AwkPreAssignment& assignment);
    void AssignName(const std::string& name, const Value& value);  // -v and var=value
    bool CompileLiteralRegexes();            // every /re/ in the program, before anything runs
    void CompileRegexesExpr(const Expr& expr);   // the walk's one expression
    void CompileRegexesStmt(const Stmt& stmt);   // the walk's one statement

    // --- running ---
    Value ValueOf(const Expr& expr);         // an expression's value
    Flow RunStatement(const Stmt& stmt);     // one statement, and what it tells the ones around it
    Flow RunMainItems();                     // one record's items, in order
    void RunBeginItems();
    void RunMainLoop();
    void RunEndItems();

    // --- names and values ---
    Variable& GlobalVariable(int slot);
    Value& ScalarRef(const Expr& variable);  // a Variable expression's scalar (makes an Untyped one Scalar)
    Value& ScalarRef(int slot, const std::string& name);
    AwkArray& ArrayRef(int slot, const std::string& name);  // makes an Untyped one Array
    // A parameter's name in gawk's error texts: "a" alone, or "a (from x)"
    // when the argument was passed by name from x.
    std::string VariableName(int localSlot, const std::string& name);
    // A local parameter's scalar or array, through its binding chain to the
    // variable the argument came from (see Variable::binding). ScalarRef
    // types every still-Untyped variable on the chain; ArrayRef creates the
    // array at the chain's end.
    Value& ScalarRef(Variable& variable, const std::string& name);
    AwkArray& ArrayRef(Variable& variable, const std::string& name);
    // The scalar or array a resolved Variable/Index/In expression names:
    // global (localSlot -1) or the current call's parameter.
    Value& ScalarRefOf(int slot, int localSlot, const std::string& name);
    AwkArray& ArrayRefOf(int slot, int localSlot, const std::string& name);
    // The current call's frame; null in the items themselves.
    ActiveCall* CurrentCall();
    // A Variable expression's own Variable, global (localSlot -1) or the
    // current call's parameter.
    Variable& VariableOf(const Expr& variable);
    void Assign(const Expr& lvalue, const Value& value);    // Variable, Index, Field (and NF)
    // An lvalue with its subscript or field index evaluated once, so a
    // compound assignment or ++/-- reads and writes the same place.
    struct Place {
        const Expr* lvalue = nullptr;
        std::string key;      // Index: the subscript
        intmax_t field = 0;   // Field: the index
    };
    Place PlaceOf(const Expr& lvalue);
    Value ReadPlace(const Place& place);
    void WritePlace(const Place& place, const Value& value);
    // A Variable assignment: NF lives in the FieldStore; FS is checked
    // against the regex engine (located: with the statement's position, as
    // a program assignment reports it; false for -v, -F and var=value).
    void AssignSlot(int slot, const std::string& name, const Value& value, bool located = true);
    std::string Subscript(const std::vector<ExprPtr>& subscripts);  // ToString(CONVFMT) joined by SUBSEP
    const std::string& SpecialString(int slot);  // FS, OFS, ORS, RS, SUBSEP, CONVFMT, OFMT as strings
    // FS with two or more bytes must compile as an ERE: gawk checks it when
    // it is assigned, not when a record is first split. The fatal and the
    // escape warnings are located as AssignSlot's.
    void CheckFieldSeparator(bool withLocation);

    // --- items and input ---
    bool MatchesPattern(const Item& item, size_t itemIndex);  // ranges kept per item
    bool NextMainRecord();                   // next record of the operands into $0; false at the end
    bool OpenNextInput();                    // the next ARGV element that is an input; false at the end
    void Output(const Stmt& print, const std::string& text);  // print's bytes: stdout here; awk--io adds redirections
    void SplitRecord(std::string_view record, const std::string& fs, bool paragraphMode,
                     std::vector<std::string>& fields);       // the FieldStore's Splitter

    // --- regexes ---
    bool MatchRegex(const Expr& regex, const std::string& text);  // a literal or dynamic regex against text
    bool MatchRegexLiteral(const Expr& regex, const std::string& text);  // /re/ itself, compiled already
    // The regex of a built-in's argument (sub/gsub/match/split): a
    // non-parenthesized /re/ itself, a dynamic value through the cache.
    std::shared_ptr<const Regex> RegexOperand(const Expr& operand);
    // The escape warnings of a regex, once per message per run: reported
    // through FormatAwkWarning before the run, at the run's location in it.
    void RegexWarnings(const std::vector<std::string>& messages, bool atRuntime);
    // A run-time warning at the place being run (the location prefix, the
    // FILENAME part past the first record); awk--functions reuses it.
    void RuntimeWarning(const std::string& message);

    // --- the hooks awk's later tasks fill ---
    Value CallBuiltin(const Expr& call);     // AwkFatal here; awk--functions implements
    Value CallFunction(const Expr& call);    // AwkFatal here; awk--functions implements
    Value EvaluateGetline(const Expr& expr); // AwkFatal here; awk--io implements

    // --- errors and stopping ---
    void ReportFatal(const AwkFatal& error); // gawk's "fatal:" line, status 2
    void ThrowIfStopped();                   // throws Stopped when a stop was asked for
    std::string LocatedPrefix();             // "awk: <source>:<line>[: (FILENAME=f FNR=n) ]"
    std::string SourceNameAt(const SourcePosition& position); // its source's name

    BuiltinContext& m_context;
    std::shared_ptr<const Program> m_program;
    const AwkInvocation& m_invocation;
    // A deque: a parameter's binding points into the caller's own globals,
    // and a vector's reallocation on a new slot would leave them dangling.
    std::deque<Variable> m_globals;
    std::unordered_map<std::string, int> m_globalSlots;
    std::unordered_map<std::string, int> m_functionSlots;  // a function's name -> its index in Program::functions
    // The active calls, outermost first; empty in the items themselves.
    std::vector<std::unique_ptr<ActiveCall>> m_calls;
    ItemKind m_currentItemKind = ItemKind::Main;  // next/nextfile out of a function: BEGIN and END refuse them
    FieldStore m_fields;
    SourcePosition m_position;               // of the statement (or pattern) being run: fatal errors report it
    std::string m_specialStrings[kSpecialSlotCount];  // SpecialString's per-slot storage
    std::vector<bool> m_inRange;             // a range item's "in range" flag, per item
    std::shared_ptr<RecordReader> m_reader;  // the current input; null between operands
    intmax_t m_argvIndex = 1;                // the next ARGV element to consider
    bool m_openedInput = false;              // an input was opened (else stdin is read once)
    AwkRegexCache m_regexCache;              // the program's dynamic regexes, by their awk text
    std::unordered_set<std::string> m_regexWarningsGiven;  // the escape warnings already reported
    bool m_regexCompileFailed = false;       // a literal regex did not compile: the walk stopped
    intmax_t m_exitCode = 0;
    bool m_exitFromBegin = false;            // exit in BEGIN: the input is skipped
};

} // namespace Haisos::Awk