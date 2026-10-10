#pragma once
#include <cstdint>
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

// A variable's storage. Untyped until first used: as a scalar (reading it
// too) it becomes Scalar, as an array Array; the other use is then an
// AwkFatal. The array is shared so awk--functions can pass it by reference.
struct Variable {
    enum class Kind { Untyped, Scalar, Array };
    Kind kind = Kind::Untyped;
    Value scalar;
    std::shared_ptr<AwkArray> array;
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
    void ResolveExpr(const Expr& expr);     // gives every name its slot
    void ResolveStmt(const Stmt& stmt);
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
    void AssignSlot(int slot, const std::string& name, const Value& value);  // a Variable assignment: NF lives in the FieldStore
    std::string Subscript(const std::vector<ExprPtr>& subscripts);  // ToString(CONVFMT) joined by SUBSEP
    const std::string& SpecialString(int slot);  // FS, OFS, ORS, RS, SUBSEP, CONVFMT, OFMT as strings

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
    std::vector<Variable> m_globals;
    std::unordered_map<std::string, int> m_globalSlots;
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