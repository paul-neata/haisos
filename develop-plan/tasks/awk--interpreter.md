# Task awk--interpreter: run awk programs

- Rock: awk
- Depends on: awk--parser, awk--values, coreutils--sort (`BuiltinText.h`: `OpenInputOperand`)
- Size: ~1000 changed lines in ~6 files
- Plan checked against: develop @ ccb9dbe
- PR title: Run awk programs: variables, fields, patterns, control flow, print

## Goal

`awk` runs programs: `BEGIN`, pattern-action items and `END`; range
patterns (`p1, p2`); variables, `-v` and `var=value` operands; fields
(`$0`, `$n`, `NF`, assignment rebuilding `$0` with `OFS`) split by the
default `FS` or a single character (`-F,`, `-F '\t'`; `-F t` is a `t`);
records separated by `RS`'s first character; `NR`, `FNR`, `FILENAME`;
associative and multi-dimensional arrays (`in`, `delete`, `SUBSEP`);
`if`, `while`, `do`, `for`, `for (k in a)`, `break`, `continue`, `next`,
`nextfile`, `exit [code]`; every operator with POSIX's string/number
comparison rules; `print` to standard output with `OFS`/`ORS`/`OFMT`;
`ARGV`/`ARGC` (edits in `BEGIN` change what is read). Files and standard
input are read through `ICurrentProcess::IO()`. Runtime errors are gawk's
(`awk: cmd. line:1: (FILENAME=- FNR=1) fatal: division by zero
attempted`, status 2). So `awk -F, 'NR > 1 { s += $3 } END { print s }'
data.csv` prints gawk's output.

Not yet (each a clear fatal error until its task): regular expressions --
`/re/`, `~`, `!~`, regex `FS`, `RS = ""` (awk--records); built-in and
user function calls, `printf` (awk--functions); `getline`, output
redirections (awk--io).

## Context

Read first: `src/components/BuiltinCommands/commands/awk/CLAUDE.md` and
every file it lists; root `CLAUDE.md` ("Security", "Exit codes");
`src/components/BuiltinCommands/CLAUDE.md` ("Output": buffering, broken
pipes).

What earlier tasks provide (on develop; their plans are the authority):
- awk--parser: `ParseAwkProgram(sources)` -> `ParseResult { program,
  diagnostics, failed }`; the AST (`Program`, `Item`/`ItemKind`,
  `FunctionDefinition`, `Stmt`/`StmtKind`/`RedirectKind`,
  `Expr`/`ExprKind`/`ExprOp`/`GetlineForm`, `SourcePosition`), `DumpProgram`.
- awk--lexer: `AwkInvocation` (`preAssignments`, `programFiles`,
  `programText`, `operands`), `ParseAwkInvocation`, `LoadAwkSources`,
  `DecodeAwkStringEscapes`, `AwkLocationPrefix`, `kAwkName`.
- awk--values: `Value` (`FromNumber/FromString/FromInput`, `ToNumber`,
  `ToString(format)`, `ToBoolean`, `IsNumeric`), `CompareValues`,
  `AwkNumberToString`, `IsAwkIdentifier`, `AwkArray`, `SplitAwkFields`,
  `FieldStore(Splitter)`, `RecordReader`/`RecordReadResult`, `AwkFatal`.
- coreutils--sort: `OpenInputOperand(context, name, failure)` /
  `InputOpenFailure`.

Every expected output below was checked with gawk 5.2.1 `--posix`; the
task container has only mawk -- never change an expectation to mawk's.

## Changes

### `commands/awk/AwkAst.h`

Add resolution fields the interpreter fills (`mutable`, so it works on a
`const Program`): `Expr::slot` (`mutable int slot = -1`: Variable, Index,
In, the global slot of the name), `Stmt::slot` and `Stmt::arraySlot`
(`mutable int`, ForIn's variable and array, Delete's array). Nothing else
in the AST changes.

### `commands/awk/AwkInterpreter.h` / `AwkInterpreter.cpp` (new)

A plain class (no interface). The structure is fixed here because
awk--records, awk--functions and awk--io add to it:

```cpp
// A variable's storage. Untyped until first used: as a scalar (reading it
// too) it becomes Scalar, as an array Array; the other use is then an
// AwkFatal. The array is shared so awk--functions can pass it by reference.
struct Cell {
    enum class Kind { Untyped, Scalar, Array };
    Kind kind = Kind::Untyped;
    Value scalar;
    std::shared_ptr<AwkArray> array;
};

// The special variables, at these fixed global slots (registered first, in
// this order); NF's cell is unused -- NF lives in the FieldStore.
enum SpecialSlot : int {
    kSlotFS, kSlotOFS, kSlotORS, kSlotRS, kSlotSUBSEP, kSlotCONVFMT, kSlotOFMT,
    kSlotNR, kSlotFNR, kSlotFILENAME, kSlotRSTART, kSlotRLENGTH,
    kSlotARGC, kSlotARGV, kSlotENVIRON, kSlotNF, kSpecialSlotCount,
};

// What a statement tells the statements around it.
enum class Flow { Normal, Break, Continue, Next, NextFile, Exit, Return };

class Interpreter {
public:
    Interpreter(BuiltinContext& context, std::shared_ptr<const Program> program,
                const AwkInvocation& invocation);
    // Runs the program; returns awk's exit status: the exit code (0 by
    // default), 2 after a fatal error (reported), 143 when stopped.
    int Run();

private:
    void Prepare();                          // resolve names to slots; set the special variables; -F/-v
    Value Evaluate(const Expr& expr);
    Flow Execute(const Stmt& stmt);
    Cell& GlobalCell(int slot);
    Value& ScalarRef(const Expr& variable);  // a Variable's scalar (makes an Untyped one Scalar)
    AwkArray& ArrayRef(int slot, const std::string& name);  // makes an Untyped one Array
    void Assign(const Expr& lvalue, const Value& value);    // Variable, Index, Field (and NF)
    std::string Subscript(const std::vector<ExprPtr>& subscripts);  // ToString(CONVFMT) joined by SUBSEP
    const std::string& SpecialString(int slot);             // FS, OFS, ORS, RS, SUBSEP, CONVFMT, OFMT as strings
    bool MatchesPattern(const Item& item, size_t itemIndex);  // ranges kept per item
    bool NextMainRecord();                   // next record of the operands into $0; false at the end
    void Output(const Stmt& print, const std::string& text);  // print's bytes: stdout here; awk--io adds redirections
    void SplitRecord(std::string_view record, const std::string& fs, bool paragraphMode,
                     std::vector<std::string>& fields);       // the FieldStore's Splitter
    Value CallBuiltin(const Expr& call);     // AwkFatal here; awk--functions implements
    Value CallFunction(const Expr& call);    // AwkFatal here; awk--functions implements
    Value EvaluateGetline(const Expr& expr); // AwkFatal here; awk--io implements
    void ThrowIfStopped();                   // throws when stopped (see Stopping)
    ...
    BuiltinContext& m_context;
    std::shared_ptr<const Program> m_program;
    std::vector<Cell> m_globals;
    std::unordered_map<std::string, int> m_globalSlots;
    FieldStore m_fields;
    SourcePosition m_position;               // of the statement (or pattern) being run: fatal errors report it
    int m_exitCode = 0;
    ...
};
```

(The type `ThrowIfStopped` throws is private.) Other private helpers are
the implementer's.

**Prepare** (before anything runs):
1. Walk the whole program (items, functions' bodies, every expression and
   statement) and give every name a global slot (`m_globalSlots`; the
   specials first, at their `SpecialSlot`s), stored in `Expr::slot`,
   `Stmt::slot`/`arraySlot`. (awk--functions will resolve function
   parameters to locals instead.)
2. Specials: FS `" "`, OFS `" "`, ORS `"\n"`, RS `"\n"`, SUBSEP `"\034"`,
   CONVFMT and OFMT `"%.6g"`, NR and FNR 0, FILENAME `""`, RSTART 0,
   RLENGTH -1 (numbers); ARGV an array with `ARGV[0] = "awk"` and
   `ARGV[i] = operands[i-1]` (FromInput), ARGC the operand count + 1;
   ENVIRON an empty array (awk--io fills it).
3. `preAssignments` in order: `-F` sets FS to `DecodeAwkStringEscapes` of
   the value (`-F '\t'` is a tab; `-F t` stays `t`, as gawk `--posix`);
   `-v name=value`: name must be `IsAwkIdentifier`, else
   `AwkFatal("`1x' is not a legal variable name", /*withLocation=*/false)`;
   the value is decoded and stored `FromInput` (escape warnings are written
   as `awk: warning: <message>\n`).

**Run**:
1. `Prepare`; the BEGIN items in order. `exit` in BEGIN skips the input
   and goes to END.
2. Unless the program has no Main and no END item (then no input is read),
   loop `NextMainRecord()`; for each record, the Main items in order: a
   match (no pattern: always; a pattern: `ToBoolean` of it; a range: see
   below) runs the action, or, with no action, `print` of `$0`. `next` ends
   this record's items; `nextfile` also closes the current input (the next
   operand follows); `exit` stops the input.
3. The END items, unless an `exit` came from END already; `$0`, `NF` keep
   the last record. `exit` in END stops at once. `exit` without an
   expression keeps the code set before (`{ exit 4 } END { exit }` -> 4).
4. Return `m_exitCode` (truncated to int; the process takes it modulo 256).

**Ranges** (`p1, p2`): per item, a flag "in range". Not in range: if `p1`
matches, the record matches, and if `p2` also matches the same record the
range ends at once, else it is now in range. In range: the record matches,
and if `p2` matches the range ends. (`NR==1, NR==1` matches only record 1.)

**NextMainRecord** -- the operands, as ARGV is now (BEGIN may have changed
ARGV and ARGC): keep an index starting at 1, read while it is below ARGC's
current numeric value. An element absent from ARGV, or empty, is skipped.
An element `name=value` with `IsAwkIdentifier(name)` is an assignment made
now (decoded, `FromInput`; a special variable too: `OFS=-`, `FS=,`). Any
other element is an input: `-` is descriptor 0, else `OpenInputOperand`:
`Missing`/`BadDescriptor` -> `AwkFatal("cannot open file `<name>' for
reading: No such file or directory", false)`, `Directory` -> `... Is a
directory`, `Denied` -> `... Permission denied`. On opening: FILENAME =
the element, FNR = 0. When the operands are used up and no input was
opened at all, standard input is read once, with FILENAME `-` (gawk
`--posix`). Each record: `RecordReader::Next(SpecialString(kSlotRS))` --
`RS == ""` is `AwkFatal("RS = \"\" (paragraph mode) is not implemented
yet")` until awk--records --; NR and FNR + 1; `m_fields.SetRecord(record,
FS, false)`. `Error` -> `AwkFatal("error reading input file `<name>':
Input/output error", false)`.

**SplitRecord**: `SplitAwkFields`; when it returns false (FS of two or more
bytes), `AwkFatal("regular expression field separators are not
implemented yet")` (awk--records replaces).

**Evaluation** (`Evaluate`):
- Number, String constants; Variable -> its scalar (`NF` -> `m_fields.NF()`;
  an Array cell -> AwkFatal ``attempt to use array `x' in a scalar
  context``); Field -> `m_fields.Field(index)` (index: ToNumber truncated
  toward zero); Index -> the element (created if absent; a Scalar cell ->
  ``attempt to use scalar `x' as an array``); In -> `Contains` (never
  creates).
- Unary: `-`, `+` (numbers), `!` (1/0 of `ToBoolean`). Binary: `+ - * / %
  ^` on numbers (`/` and `%` by zero: AwkFatal `division by zero
  attempted` / ``division by zero attempted in `%'``; `%` is `fmod`, `^`
  `pow`); Concat -> String of the two `ToString(CONVFMT)`; `< <= == != > >=`
  -> `CompareValues` -> 1/0; `&&`, `||` short-circuit -> 1/0; Conditional.
- Assign: the value (compound ops on numbers, with the same division
  errors) stored by `Assign`, which handles Variable (NF -> `SetNF`), Index,
  Field (`SetField(index, value, OFS, CONVFMT)`); the result is the value
  stored. IncDec likewise; post forms return the old number.
- Regex, Match, NoMatch: `AwkFatal("regular expressions are not implemented
  yet")`. Call/BuiltinCall -> `CallFunction`/`CallBuiltin`, Getline ->
  `EvaluateGetline`: each `AwkFatal("function `<name>' is not implemented
  yet")`, resp. `AwkFatal("getline is not implemented yet")`.

**Statements** (`Execute`): Expression; Block (stops at the first
non-Normal flow); If; While, Do, For (Break ends the loop, Continue goes
on, any other non-Normal flow propagates); ForIn: a snapshot of the
array's `Keys()`, each key still present at its turn assigned to the
variable as `Value::FromInput(key)` (so `k < 10` compares numerically for
`9`, as gawk); Next, NextFile, Break, Continue -> their Flow; Exit -> the
code from the expression if any, Flow Exit; Return -> AwkFatal here
(awk--functions); Delete -> `Remove` of the subscript, or `Clear`.
Print: no arguments -> `$0`; else each argument `ToString(OFMT)` joined by
OFS; then ORS; written by `Output`, which for now refuses a redirection
(`AwkFatal("output redirection is not implemented yet")`) and else calls
`m_context.Out`. Printf: `AwkFatal("printf is not implemented yet")`.

**Fatal errors**: `Run` catches `AwkFatal` and writes, with location,
`AwkLocationPrefix(sources[m_position.source].name, m_position.line)` +
(`(FILENAME=<FILENAME> FNR=<FNR>) ` when FNR's numeric value is above 0) +
`fatal: <message>\n`; without location `awk: fatal: <message>\n`; status 2.
`m_position` is set before each statement and each pattern is evaluated.

**Stopping**: check `context.StopRequested()` before each record and at
each loop iteration (while, do, for, for-in); when set, unwind (a private
exception) and return 143. A broken pipe on stdout stops the process the
same way (BuiltinContext calls `StopForBrokenPipe`, which sets the stop).

### `commands/awk/Awk.cpp`

`Run`: after a successful parse, `Interpreter(context, program,
invocation).Run()`; an empty program (`''`) runs nothing and returns 0
before reading anything. Remove the not-implemented message. `Help().notes`
gains: insertion-ordered `for (k in a)`; regexes, functions, printf,
getline and redirections are not available yet (removed by their tasks).

### Build

`src/components/BuiltinCommands/CMakeLists.txt`: add `commands/awk/AwkInterpreter.cpp`.

### Rules that apply

`ICurrentProcess` is the only door out: input files through
`context.IO()` (`OpenInputOperand`), standard input through
`GetDescriptor(0)`, output through `context.Out`; nothing holds an
`IFileSystem` or `IHaisosOS`. Portable C++17.

## Tests

`tests/unit/components/Awk.unittests/AwkInterpreterTest.cpp` (new, in the
CMakeLists): `class AwkRunTest : public BuiltinCommandsTest` whose
`SetUp` also writes `/abc.txt` (`a b c\nd e f\n`) and `/data.csv`
(`x,1,2.5\ny,2,3.25\nz,3,4\n`); `TEST_F`s on `RunCaptured("awk", args,
input)`, checking `out`, `err` and `status` exactly (status 0 and empty
err unless given). Table-driven within each test is fine.

- `BeginOnlyAndEmptyProgram`: `BEGIN { print "b" }` with input `x\n` -> `b\n`; `''` -> nothing.
- `FieldsAndCounters`: `{ print NR, FNR, FILENAME, NF, $2 }` `/abc.txt /abc.txt`
  -> `1 1 /abc.txt 3 b\n2 2 /abc.txt 3 e\n3 1 /abc.txt 3 b\n4 2 /abc.txt 3 e\n`;
  input `a\n`, `{ print "[" FILENAME "]" }` -> `[-]\n`.
- `SumsAColumn`: `-F, 'NR > 1 { s += $3 } END { print s }' /data.csv` -> `7.25\n`.
- `OperandAssignments`: `-F, '{ print $1, $2 * 2 }' OFS=- /data.csv` -> `x-2\ny-4\nz-6\n`;
  `'{ print x }' x=1 /abc.txt x=2 /abc.txt` -> `1\n1\n2\n2\n`; `'BEGIN { print v }' v=1` -> `\n`;
  input `z\n`, `'{ print v }' 'v=a\tb'` -> `a\tb\n` (a real tab); input `a b\n`, `'{ print $1 }' ''` -> `a\n`.
- `DashV`: `-v n=5 -v 's=a\nb' 'BEGIN { print n + 1; print s }'` -> `6\na\nb\n`;
  `-v 1x=3 'BEGIN { }'` -> err ``awk: fatal: `1x' is not a legal variable name\n``, 2.
- `FieldAssignment`: `'{ $7 = "z"; print; print NF }' OFS=: /abc.txt` -> `a:b:c::::z\n7\nd:e:f::::z\n7\n`;
  `'{ $0 = "q r"; print $2, NF }' /abc.txt` -> `r 2\nr 2\n`;
  `'BEGIN { $0 = "a b c"; NF = 2; print $0; $0 = "  x  "; print NF, "[" $1 "]" }'` -> `a b\n1 [x]\n`;
  input `a b c\n`, `'{ NF = 5; print; print NF; $2 = ""; print; print NF }'` -> `a b c  \n5\na  c  \n5\n`;
  `'BEGIN { NF = 2; print "[" $0 "]"; $2 = "b"; print "[" $0 "]" }'` -> `[ ]\n[ b]\n`.
- `FieldSeparators`: input `a\tb  c\n`, `-F '\t' '{ print $2 }'` -> `b  c\n`;
  input `atb\tc\n`, `-F t '{ print $2 }'` -> `b\tc\n`; input `a|b.c\n`,
  `-F '|' '{ print $2 }'` -> `b.c\n` and `-F . '{ print $2 }'` -> `c\n`;
  input `a:b:c\n`, `'BEGIN { FS = ":" } { print $2 }'` -> `b\n`;
  input `a:b\nc:d\n`, `'{ FS = ":"; print $1 }'` -> `a:b\nc\n`;
  input `abc\n`, `'BEGIN { FS = "" } { print NF, $1 }'` -> `1 abc\n`;
  input `aXbXc`, `'BEGIN { RS = "X" } { print NR, $0 }'` -> `1 a\n2 b\n3 c\n`;
  input `xaby\nzabw`, `'BEGIN { RS = "ab" } { print NR ": " $0 }'` -> `1: x\n2: by\nz\n3: bw\n`.
- `Comparisons`: `'BEGIN { print 1 == 1.0, "10" < "9", 10 < 9, "a" < "b", x == 0, x == "" }'`
  -> `1 1 0 1 1 1\n`; input `10 9\n`, `'{ print ($1 < $2), ($1 < "9"), ($1+0 < $2) }'`
  -> `0 1 0\n`; input `10\n9\n`, `'{ a[NR] = $1 } END { print (a[1] < a[2]) }'` -> `0\n`;
  `'BEGIN { a[9]; a[10]; for (k in a) if (k < 10) print "lt", k }'` -> `lt 9\n`.
- `ArithmeticAndTruth`: `'BEGIN { print -3 % 2, 2 ^ 3 ^ 2, -2 ^ 2, 2 ^ -1, 7 / 2, 1e6 * 1e6, 0.1 * 3, 100 / 3 }'`
  -> `-1 512 -4 0.5 3.5 1000000000000 0.3 33.3333\n`;
  `'BEGIN { x = "3x"; print x + 1, +"", -"", !"", !"0", !0, !"a" }'` -> `4 0 0 1 0 1 0\n`;
  input `0\n`, `'{ print !$1, ($1 ? "t" : "f") }'` -> `1 f\n`.
- `NumberOutput`: `'BEGIN { print 1e30, -1e30, 2^64, 0.1+0.2, 1e-5, 123456.7, 1234567.8; x = 1e30; y = x ""; print y; CONVFMT="%d"; z = 2.5 ""; print z; OFMT="%x"; print 255.5; z=-0; print z, -0.0; print 2^31, -2^63 }'`
  -> `1000000000000000019884624838656 -1000000000000000019884624838656 18446744073709551616 0.3 1e-05 123457 1.23457e+06\n1000000000000000019884624838656\n2\nff\n0 0\n2147483648 -9223372036854775808\n`;
  `'BEGIN { CONVFMT = "%.2f"; a = 3.14159; b = a ""; print b; print a; OFMT = "%.1f"; print a, a "" }'`
  -> `3.14\n3.14159\n3.1 3.14\n`;
  input `0x11\n+inf\nnan\n -3.5e2x\n1e400\n.5.\n 12 \n1e\n`,
  `'{ print $1+0, ($1 == $1+0) ? "strnum" : "str" }'` ->
  `17 strnum\n+inf strnum\n+nan str\n-350 str\n+inf strnum\n0.5 str\n12 strnum\n1 str\n`.
- `Arrays`: `'BEGIN { x["a"] = 1; x["b"]; if ("b" in x) print "in"; delete x["a"]; for (k in x) print k; delete x; for (k in x) print "left"; print "ok" }'`
  -> `in\nb\nok\n`; `'BEGIN { a[1,2] = 3; for (k in a) if (k == 1 SUBSEP 2) print "joined"; if ((1,2) in a) print "yes" }'`
  -> `joined\nyes\n`; `'BEGIN { SUBSEP = ":"; a["x","y"]; for (k in a) print k }'` -> `x:y\n`;
  `'BEGIN { x = 0.1 + 0.2; a[x]; for (k in a) print k; print (0.3 in a) }'` -> `0.3\n1\n`;
  `'BEGIN { a["z"]; a["a"]; a["m"]; for (k in a) print k }'` -> `z\na\nm\n`
  (Haisos's insertion order; gawk's is unspecified -- say so in a comment).
- `ControlFlow`: `'BEGIN { while (i < 3) { i++; if (i == 2) continue; print i }; do { j++ } while (j < 5); print j; for (k = 0; k < 10; k++) if (k == 3) break; print k }'`
  -> `1\n3\n5\n3\n`.
- `NextExitNextfile`: `'{ next; print "no" } END { print NR }' /abc.txt` -> `2\n`;
  `'{ print; exit 5 } END { print "end", NR }' /abc.txt` -> `a b c\nend 1\n`, 5;
  `'BEGIN { exit } END { print "end" }'` -> `end\n`, 0; `'BEGIN { exit 3 } END { print "end" }'` -> `end\n`, 3;
  input `q\n`, `'{ exit 4 } END { print "end"; exit }'` -> `end\n`, 4;
  `'FNR == 1 { print FILENAME; nextfile } { print "never" }' /abc.txt /data.csv` -> `/abc.txt\n/data.csv\n`.
- `Ranges`: `'NR==1, NR==1 { print "one", NR }' /abc.txt` -> `one 1\n`;
  `'$1 == "a", $1 == "d" { print "r", $1 }' /abc.txt` -> `r a\nr d\n`;
  `'$1 == "d", 0' /abc.txt` -> `d e f\n`.
- `ArgvAndArgc`: `'BEGIN { print ARGC, ARGV[0], ARGV[1] }' x` -> `2 awk x\n`;
  `'BEGIN { ARGV[1] = "/abc.txt"; ARGC = 2 } { print FILENAME, $0 }' /nope`
  -> `/abc.txt a b c\n/abc.txt d e f\n`.
- `RuntimeErrors` (status 2; err exactly):
  `'BEGIN { z = 0; x = 1 / z }'` -> `awk: cmd. line:1: fatal: division by zero attempted\n`;
  input `a\n`, `'{ z = 0; x = 1 / z }'` -> `awk: cmd. line:1: (FILENAME=- FNR=1) fatal: division by zero attempted\n`;
  `'BEGIN { z = 0; print 1 % z }'` -> ``awk: cmd. line:1: fatal: division by zero attempted in `%'\n``;
  `'BEGIN { x[1] = 1; x = 2 }'` -> ``awk: cmd. line:1: fatal: attempt to use array `x' in a scalar context\n``;
  `'BEGIN { x = 1; x[1] = 2 }'` and `'BEGIN { if (x == 0) print "z"; x[1] = 1 }'` (out `z\n`)
  -> ``awk: cmd. line:1: fatal: attempt to use scalar `x' as an array\n``;
  `'BEGIN { print $(-1) }'` -> `awk: cmd. line:1: fatal: attempt to access field -1\n`;
  `'BEGIN { NF = -1 }'` -> `awk: cmd. line:1: fatal: NF set to negative value\n`;
  `'{ print }' /nope` -> ``awk: fatal: cannot open file `/nope' for reading: No such file or directory\n``;
  `'{ print }' /docs` -> ``awk: fatal: cannot open file `/docs' for reading: Is a directory\n``;
  `/dz.awk` written as `BEGIN {\n z=0\n x = 1/z }`, `-f /dz.awk` -> `awk: /dz.awk:3: fatal: division by zero attempted\n`.
- `NotYetAvailable`: `'BEGIN { if ("a" ~ /a/) print 1 }'` -> err
  `awk: cmd. line:1: fatal: regular expressions are not implemented yet\n`, 2
  (each later task changes its own such test).
- `StopsPromptly`: stdin the read end of a pipe (`os->GetPipeService()->CreatePipe()`,
  the test keeping the write end open), `StartProcess` of `/bin/awk {
  print }`, `TriggerStop()`; `WaitToFinish(1000)` true and exit code 143.
  Also `BEGIN { while (1) x++ }` stopped the same way.
- `BrokenPipeExits141`: stdout the write end of a pipe whose read end is
  reset; `BEGIN { while (1) print "y" }` -> exit code 141.

Remove `AwkCommandTest.ParsedProgramsDoNotRunYet` and the not-implemented
expectations of awk--lexer's `AwkCommandTest` cases (they now run).

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Awk
bash ./scripts/test_linux.sh L U
```

## Docs

`commands/awk/CLAUDE.md`: a "Running" section -- the `Interpreter` and its
fixed structure (Cell, the special slots, Flow, the hook methods later
tasks fill: `CallBuiltin`, `CallFunction`, `EvaluateGetline`, `Output`,
`SplitRecord`), Prepare, the run order, ranges, operands and ARGV, fatal
error format, stopping. `src/components/BuiltinCommands/CLAUDE.md` and
root `CLAUDE.md`: the awk rows describe what runs now and what does not yet.

## Acceptance

- [ ] Every test above passes with the exact output; the interpreter
  structure (names above) is in place for the later tasks.
- [ ] All input through `context.IO()`, all output through `context.Out`.
- [ ] Stops within a second of `TriggerStop()`, even in an endless loop;
  141 on a broken stdout.
- [ ] Unimplemented parts fail with the stated fatal messages, never
  silently.
- [ ] All unit tests green.

## Out of scope

- Regexes in patterns and `~`, regex FS, `RS = ""` (awk--records).
- Built-in functions, user functions, `return`, `printf`/`sprintf`
  (awk--functions).
- `getline`, `>`/`>>`/`|` output, `close`, `system`, `ENVIRON`, `fflush`
  (awk--io).
