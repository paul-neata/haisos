# awk

awk is the Haisos builtin of POSIX awk, run as gawk `--posix` runs it (the
reference behaviour; gawk's manual and man page describe it). All of it lives
in namespace `Haisos::Awk`, in this directory, as plain portable C++17 (it is
built for Linux, Windows/MSVC and WASM: no POSIX headers, no `<regex>`).

The pipeline is lexer -> parser -> interpreter; each stage is a task of the
awk rock and adds its files here:

- `AwkError.h/.cpp` - the diagnostics every stage shares: `AwkSource` (a
  piece of program text: the program operand, named `cmd. line`, or one `-f`
  file, named as given), `AwkWarning` (an escape-sequence warning: source,
  line, message), `AwkSyntaxError` (message, source, line, line text and the
  caret's column) and their formatters: `FormatAwkSyntaxError` writes gawk's
  two-line report (the source line, then the same prefix, the caret padding
  one byte per source byte before the column -- a tab stays a tab, anything
  else a space -- and the `^ <message>`), `FormatAwkWarning` and
  `FormatAwkError` the `warning:`/`error:` lines, all behind
  `AwkLocationPrefix`: `awk: <source>:<line>: `. `AwkFatal` is a run-time
  "fatal:" error (the program stops, status 2; the interpreter formats it,
  with or without its location -- see "Running").
- `AwkLexer.h/.cpp` - the `Lexer` of POSIX awk as gawk `--posix` reads a
  program, and `DecodeAwkStringEscapes`, awk's string escapes (also the
  escapes of a `-v` value, `-F`'s value and `var=value` operands when the
  interpreter applies them). The rules:
  - Blanks (space, tab, CR) separate tokens; `#` comments run to the end of
    the line (its newline still a token); a backslash-newline (or
    backslash-CR-LF) is removed anywhere outside a string, regex or comment,
    and a backslash before anything else is the error `backslash not last
    character on line`.
  - Newlines are tokens except after `{ && || , ; do else` (every following
    newline skipped, blanks and comments between them included). A source
    that does not end in a newline gets one at its end; `EndOfInput`
    (repeated forever) sits at the end of the last line, an empty source at
    line 1, column 0.
  - Names are `[A-Za-z_][A-Za-z0-9_]*`: a keyword its kind (`func` is
    `function`), one of POSIX's built-in functions a `Builtin` whatever
    follows, any other name a `FuncName` only when it touches its `(`, else a
    `Name` (gawk's extension functions are plain names under `--posix`).
  - Numbers are digits with an optional fraction, or `.digits`, then an
    exponent only when digits follow (`1e` is `1` then the name `e`); `1.2.3`
    is `1.2` then `.3`, `0x11` is `0` then `x11`; the value is `std::strtod`
    of the token text.
  - A string is `"..."`, its body decoded by `DecodeAwkStringEscapes`: `\"`
    `\\` `\a` `\b` `\f` `\n` `\r` `\t` `\v`, `\ooo` (1 to 3 octal digits, a
    NUL for `\0`), any other `\c` the plain c with the warning `escape
    sequence `\c' treated as plain `c'` -- except `\x`, a plain x with no
    warning (gawk `--posix` has no `\x` escapes) -- and a trailing lone
    backslash kept. A raw newline or the end of the source before the
    closing quote is `unterminated string`, the caret on the opening quote.
  - A `/` or `/=` the parser hands back to `ScanRegex` becomes a `Regex`
    token: the text to the first `/` that is neither backslash-escaped nor
    inside a bracket expression (a `]` right after `[` or `[^` is a member;
    `[:`...`:]`, `[.`...`.]`, `[=`...`=]` are skipped whole), `\/` stored as
    `/`, every other backslash pair byte for byte. A newline or the end of
    the source first is `unterminated regexp`. Nothing else is decoded here:
    translating awk's regex escapes for the regex engine is awk--records'
    job. `ScanRegex` only takes the token `Next()` just returned.
  - Punctuation is longest match among the POSIX operators; gawk's `**`,
    `**=` and `|&` are not POSIX: `**` is two `*` tokens. A lone `&` (not
    `&&`) is `syntax error` with the caret on it, as gawk's. A byte that
    starts no token is `invalid char '<c>' in expression`.
  - A backslash ending a comment belongs to the comment: it continues
    nothing, so a source whose last line ends in such a comment ends in a
    real newline (no newline is added, `EndOfInput` on that line). Whether
    the source ends in a real newline is decided as the end is reached, from
    whether the last byte consumed was one (a `Newline` token or a skipped
    newline), not a token, blanks, a comment or a continuation.
  - A `Newline` token that ends a comment carries the comment as its `text`
    (from the `#` to before the newline, the source's added newline too when
    the comment runs to the end); every other `Newline`'s `text` is empty.
- `AwkAst.h/.cpp` - the syntax tree every POSIX awk program parses to:
  `Program` (the sources and the items, in source order) owning `Item`s
  (BEGIN, END, a pattern, a range, or a function), `FunctionDefinition`s and,
  under those, `Stmt`s and `Expr`s -- plain structs owned through
  `unique_ptr`, held by the interpreter as a `shared_ptr<const Program>`.
  `Expr::parenthesized` marks the result of a one-expression grouping
  `( e )`: never an lvalue (gawk's `(x) = 3` is a syntax error), ignored by
  the dump. The parser fills only the fields declared here; the
  interpreter's tasks add `mutable` fields for what they resolve (variable
  slots, compiled regexes) and never change these. `DumpExpr`, `DumpStmt`
  and `DumpProgram` write the one-line dumps the tests compare against
  (see "AST dump" below).
- `AwkParser.h/.cpp` - the recursive-descent parser of awk expressions
  (`Parser::ParseExpression`) and of whole programs (`ParseAwkProgram`;
  see "Parsing programs" below). The precedence chain, lowest first (one
  method per level, POSIX's table): assignment (right-associative, only an
  lvalue may be assigned to -- a parenthesized expression never one
  (`Expr::parenthesized`); a post-increment of a field takes its value
  before gawk's `cannot assign a value to the result of a field
  post-increment expression`; as gawk's, an assignment may also be either
  branch of the ternary or the right operand of `|| && ~ !~` and the
  comparisons, so `1 && y = 2` is `1 && (y = 2)`), the ternary
  (right-associative), `||`,
  `&&`, `in`
  (`match-expr { 'in' NAME }`), `~ !~` (left-associative), the comparison
  operators (non-associative), `| getline`, concatenation, `+ -`,
  `* / %`, unary `! - +`, `^` (its exponent at the unary level, so
  right-associative and `-2^2` is `-(2^2)`), `++ --` (pre on an lvalue, post
  on a primary that is one) and the primaries. The parser keeps exactly one
  token of lookahead and the lexer never reads past it, so a `/` or `/=`
  standing where an operand is expected is handed back to `Lexer::ScanRegex`
  and becomes a regex literal; anywhere else it is division. Concatenation
  continues while the current token can start an operand: Number, String,
  Name, FuncName, Builtin, `$`, `!`, `(`, `++`, `--`, `getline` -- never `+`
  or `-` (so `a -1` subtracts), `/` `/=` or `in`. A `$` takes a dollar
  operand with no postfix of its own: `++`/`--` on an lvalue, `-`/`+`/`!`
  then a dollar operand, or a primary -- so `$i++` is `($i)++`, `$++i` is
  `$(++i)` and `$NF-1` is `($NF)-1`. `getline` takes an optional lvalue
  target (a name, `name[...]` or `$...`) and then `<` and a file operand
  parsed at the `+ -` level: arithmetic, unary and `^` included, no
  concatenation, no comparison; `"cmd" | getline [target]` is the command
  form (left-associative, concatenation going on after it: `"a" | getline x
  "b"` concatenates `"b"`), and a `|` not followed by `getline` is a syntax
  error at the token after it. A `Builtin` without `(` is allowed only for
  a bare `length` (`length / 2` divides). Errors are gawk's: `syntax error` at the token
  where parsing failed, but a `Newline` there is
  `unexpected newline or end of string` reported on the line it ends (one
  more than its own), a `Newline` ending a comment is `syntax error` with
  the caret on the comment's `#`, and `EndOfInput` is
  `unexpected newline or end of string` at its own place. Lexer errors pass
  through unchanged. `ParseAwkExpression(text)` parses |text| (source
  "cmd. line") as one expression that must be the whole text -- for the
  tests.
- "Parsing programs" -- what `ParseAwkProgram` (and `Parser`'s statement,
  item and function levels) do, from POSIX's grammar and gawk's observed
  behaviour:
  - Sources are parsed one by one, each by its own `Parser`, their items
    and functions appended to one `Program` in source order -- a rule may
    not span two sources. Items: an optional leading newline or `;`, then
    `function` (`func`) Name `(` parameters `)` newlines block; `BEGIN`/`END`
    blocks; a pattern, a range `pattern, pattern`, each with an optional
    block (no block means `print $0`, a null action); a block alone.
    Newlines and `;` separate items; after an item ending in `}` the next
    may follow directly (`BEGIN{}END{}`), but a pattern-only item must end
    at a newline, `;` or the end (`NR==1\n{ print }` is two items).
  - Statements: newlines and `;` are skipped inside a block, `}` ends it;
    `if`/`else`, `while`, `do`/`while`, the classic `for` and `for`-`in`,
    `print`/`printf`, `next`, `nextfile`, `exit`/`return` (an expression
    unless a terminator or `}` follows), `break`, `continue`, `delete`, and
    an expression. A simple statement ends at `;`, a newline or nothing
    before `}`; a body that is a lone `;` is an empty block. After `for (`,
    an expression that is an `in` of one plain variable and closes at `)`
    is a `for`-`in` loop; anything else must be a classic `for` (`for ((k)
    in a)` is a syntax error at its `)`, as gawk's).
  - The print context: while `print`/`printf` arguments are parsed, `>`
    is not a comparison and `|` is not `| getline` -- both end the list and
    start a redirection (`>`, `>>`, `|`) whose target is parsed at the
    concatenation level. `(`, `[` and call arguments clear the context for
    their contents. `print (`: a parenthesized list closed by `)`, `>`,
    `>>`, `|` or a terminator is the argument list; otherwise, with one
    expression inside, it was a grouping and the first argument continues
    from it (`print (1)(2)` prints the concatenation); with several, `in`
    must follow. A grouping so continued is `parenthesized`: `print (x) = 3`
    is a syntax error at the `=`, as gawk's.
  - Parse-time errors (FormatAwkError, parsing goes on, status 1, texts
    gawk's): `` `next' used in BEGIN action `` and the END/`nextfile`
    forms; `` `break' is not allowed outside a loop or switch `` and
    `` `continue' is not allowed outside a loop ``; a function defined
    twice; a duplicated parameter, the function's own name as a parameter,
    a special variable as a parameter; and, after the whole program, a
    defined function's name used as a variable or an array
    (`` function `f' called with space between name and `(',\nor used as a
    variable or an array ``). Syntax errors (AwkSyntaxError, parsing
    stops): `` `return' used outside function context ``, `` `length' is a
    built-in function, it cannot be redefined ``, and the expression
    level's.
  - Diagnostics come out in the order found: lexer warnings, parse-time
    errors and, last, the syntax error that stopped parsing. A `-f` source
    ending inside an item reports `` source files / command-line arguments
    must contain complete functions or rules `` with the line text
    `(END OF FILE)` (see the exceptions below).
- `AwkInvocation.h/.cpp` - the command line, as gawk takes it:
  `AwkOptionTable` (every gawk option, treated or not: `-F -f -v` with their
  long names, `-h -V -P -r`, the rest marked for the not-treated report),
  `ParseAwkInvocation` (options stop at the first operand; usage errors print
  gawk's two usage lines, status 1; a `-v` value without `=` gawk's message)
  and `LoadAwkSources` (each `-f` file read whole through `OpenInputOperand`
  and `ReadWholeInput` -- `-` is standard input; an unreadable file is
  gawk's fatal, status 2, a directory its own error, status 1).
- `AwkValue.h/.cpp` - awk's values and arrays: `Value` and its four kinds,
  the conversions, the comparison and `AwkArray` (see "Values, fields and
  records" below).
- `AwkFields.h/.cpp` - the fields of the current record: `SplitAwkFields`
  and `FieldStore` (see "Values, fields and records" below).
- `AwkInput.h/.cpp` - `RecordReader`, the record reader of one input (see
  "Values, fields and records" below).
- `AwkInterpreter.h/.cpp` - the `Interpreter` that runs a parsed program
  (see "Running" below).
- `Awk.cpp` - the `awk` builtin itself: `Name`, `Version` 1.1.0, the option
  table, the gawk-based `--help` (`BuiltinHelp::basedOn`), and `Run`:
  parse the invocation, load the sources, parse the program
  (`ParseAwkProgram`) -- its diagnostics go to stderr and any failure is
  status 1 -- then hand the program and the invocation to `Awk::Interpreter`
  and return its status.

The man page is the `--help` text, like most builtins (`ManPage` is not
overridden).

## AST dump

`DumpExpr`, `DumpStmt` and `DumpProgram` write one line per node,
sub-nodes in parentheses -- the format the tests compare against, exact:

- Number: its source text (`1e3`, `.5`). String: the value escaped as
  `DescribeToken` escapes a STR (`\\ \" \n \t`, other control bytes `\ooo`)
  in quotes. Regex: `/` + the text with every `/` written `\/` + `/`.
- Variable: the name. Field: `($ e)`. Index: `name[s1, s2]`.
- Unary: `(- e)`, `(+ e)`, `(! e)`. Binary: `(<op> a b)` with `<op>` one of
  `+ - * / % ^ < <= != == > >= ~ !~ && ||` or `concat`.
- In: `(in name s1 s2)`. Conditional: `(?: c a b)`.
- Assign: `(= lv e)`, `(+= lv e)`, `(-= ...)`, `(*= ...)`, `(/= ...)`,
  `(%= ...)`, `(^= ...)`. IncDec: `(pre++ lv)`, `(pre-- lv)`,
  `(post++ lv)`, `(post-- lv)`.
- Call: `(call f a b)`, `(call f)`. BuiltinCall: a bare `length` is
  `length`; with parentheses `(name a b)`, `(length)` for `length()`.
- Getline: `(getline)`, `(getline x)`, `(getline < "f")`,
  `(getline x < "f")`, `(| "cmd" getline)`, `(| "cmd" getline x)`.
- Statements: Block `{ s1; s2 }`, empty `{ }`; Expression: its expr;
  `print`, `print a, b`, `printf "%d\n", x`, followed by ` > t`, ` >> t` or
  ` | t` when redirected; `if (c) S`, `if (c) S else S`; `while (c) S`;
  `do S while (c)`; `for (I; C; U) S` (absent parts empty, so
  `for (; ; ) S`); `for (k in a) S`; `next`, `nextfile`, `break`,
  `continue`, `exit`, `exit e`, `return`, `return e`,
  `delete a[s1, s2]`, `delete a`.
- Items: `BEGIN S`, `END S`, `P S`, `P, Q S`, `P` (no action), `S` (no
  pattern), `function f(a, b) S`. `DumpProgram` joins them with '\n', no
  final newline.

## Values, fields and records

The data layer of the interpreter (`AwkValue`, `AwkFields`, `AwkInput`),
behaviour matched to POSIX awk and the observed output of gawk `--posix`
5.2.1. The interpreter (see "Running" below) is built on these; awk--records
adds regex field separators and `RS = ""`, awk--functions and awk--io reuse
them.

A `Value` is one of four kinds: Uninitialized (what every variable starts
as, "" and 0 at once), Number, String, and *strnum* -- text that came from
input and looks numeric, so that it compares numerically while printing as
itself. StrNum comes only from `FromInput`: the fields and `$0`, the
`var=value` operands, `-v` values, for-in keys, ARGV and (later) getline.

- Conversions. A string to a number (`StringToNumber`): leading blanks
  (space `\t \n \v \f \r`) skipped, then the longest prefix `std::strtod`
  accepts -- decimal, hex `0x1A`, `inf`, `infinity`, `nan`, any case -- none
  at all is 0 (`"3x"` 3, `"info"` +inf). `LooksNumeric` says when that
  conversion took at least one byte and only blanks follow it: what makes
  input a strnum. A number to a string (`AwkNumberToString`): NaN and
  infinities as `+nan`, `-nan`, `+inf`, `-inf` (the sign bit decides); an
  integral value as its decimal integer, every digit, whatever its size and
  whatever the format (1e30 is `1000000000000000019884624838656`, `-0` is
  `0`); anything else through `FormatAwkNumber`, which applies CONVFMT or
  OFMT to the one number -- bytes copied, `%%` a `%`, the conversions parsed
  by `ParsePrintfSpec` (d i o u x X c e E f F g G a A, a negative value
  through intmax_t as glibc's printf converts it), anything else, or a format
  ending inside a specification, copied as written.
- Comparison (`CompareValues`): POSIX's rule -- numeric (by `ToNumber`)
  when both sides are numeric (Number, StrNum or Uninitialized), else the
  two `ToString(convfmt)` compared byte by byte as unsigned chars. A numeric
  comparison with a NaN on either side returns `kAwkUnordered`: the two are
  unordered, so `< <= == > >=` are all false and `!=` true. The interpreter
  maps the result to each relational operator, `kAwkUnordered` first.
- Arrays (`AwkArray`): string keys, insertion-ordered (what `for (k in a)`
  visits), a Value reference valid until its element is removed.
- The field store (`FieldStore`): $0, $1..., NF. A record is split lazily,
  with the FS in force when it was set (`SetRecord` saves it; a later change
  of FS does not affect that record), by a splitter the interpreter
  supplies (`Splitter`; `SplitAwkFields` -- FS `" "` runs of blanks, `""` no
  splitting, one other byte literal -- until awk--records' regexes and
  paragraph mode). Fields read as strnum; an assigned field keeps its type.
  Assigning to a field beyond NF extends with empty fields; a field or NF
  assignment rebuilds $0 with the OFS (and CONVFMT for Number fields) of
  that assignment, and the rebuilt $0 is a plain String, not a strnum, while
  the fields themselves still read as strnum; `$0 = v` sets a new record,
  re-split with the FS saved with the current record. `NF = n` truncates to
  the first n fields or extends with empty ones. An assignment may make at
  most `kAwkMaxFields` (1000000) fields; a larger `$n = v` or `NF = n` is
  refused (``NF set to N: more than 1000000 fields`` / ``attempt to assign
  field N: more than 1000000 fields``), while reading `$n` past the limit
  stays quiet, returning an empty string.
- The record reader (`RecordReader`): one input, read in 64 KiB blocks,
  each record separated by the first byte of RS as it is at that call (a
  change applies from the next record on; `RS = ""` paragraph mode is
  awk--records'). The bytes past the record stay in the reader's own buffer
  for the next call, the buffer's consumed front erased before a block is
  appended so many small records cannot grow it without bound; it stops
  promptly on a stop (checked before each read) or a `kIOInterrupted`
  read, and reports an error on any other negative read.

## Running

`AwkInterpreter.h/.cpp` -- the `Interpreter`, one instance per run of the
command (`Awk.cpp` builds it with the parsed program and the invocation).
Its shape is fixed: the later tasks of the rock (records, functions, io)
fill its hooks without changing it.

- `Variable` is a global's storage: Untyped until first used, then Scalar
  or Array (the other use is a fatal error; the array is shared, for
  awk--functions' by-reference parameters). The special variables live at
  fixed `SpecialSlot`s, registered first, by name: FS, OFS, ORS, RS,
  SUBSEP, CONVFMT, OFMT, NR, FNR, FILENAME, RSTART, RLENGTH, ARGC, ARGV,
  ENVIRON, NF -- NF's variable is unused, NF living in the `FieldStore`
  (a read goes to `NF()`, an assignment to `SetNF`). `Flow` is what a
  statement tells the ones around it (Normal, Break, Continue, Next,
  NextFile, Exit, Return).
- `Prepare` resolves every name in the program to a global slot
  (`ResolveExpr`/`ResolveStmt`), sets the specials (ARGV[0] "awk" and then
  the operands, ARGC one more than they, ENVIRON an empty array) and
  applies the pre-assignments in order: `-F` to FS, `-v name=value` (and
  the operands' `var=value`) through `AssignName`; a `-v` name that is not
  an identifier is gawk's fatal `` `' is not a legal variable name ``.
- The run order is gawk's: `RunBeginItems`, then the main loop (skipped
  when BEGIN ran `exit`, and when the program has no main item and no END
  item, the input never read), then `RunEndItems`. Each record comes from
  `NextMainRecord` into $0 (NR and FNR up), then `RunMainItems` runs the
  main items in order. `exit` skips the rest of the records and the items
  but not the END items; `next` the rest of this record's items; `nextfile`
  also the rest of this file.
- Ranges and operands: a range pattern's in-range state is kept per item
  (`MatchesPattern`); a pattern-only item is a `print $0`. After BEGIN,
  `OpenNextInput` walks ARGV from element 1 on (a BEGIN edit of
  ARGC/ARGV changes what is read): a missing or empty element is skipped,
  `name=value` with a legal identifier is assigned, anything else is
  opened for reading (`-` too, the standard input, named `-`; a failure
  is gawk's fatal ``cannot open file `X' for reading: <reason>``); when
  nothing was opened, the standard input is read once.
- `ValueOf` evaluates expressions on the `AwkValue` conversions (CONVFMT
  for the implicit one, OFMT for print), POSIX's comparison rule with
  `kAwkUnordered` mapped per relational operator, concatenation joining
  through CONVFMT. Integer values -- a field index, NF, an exit code,
  ARGV's walk -- go through `AwkIntegerOf`: truncated toward zero, NaN
  and out-of-intmax values INTMAX_MIN. `Output` is print's one door: its
  text (the arguments joined by OFS, then ORS; an empty list prints $0)
  written to stdout here, awk--io adding the redirections through it.
  `SplitRecord` is the FieldStore's splitter (a regex FS or paragraph
  mode refused for now).
- The hooks later tasks fill: `CallBuiltin` and `CallFunction`
  (awk--functions), `EvaluateGetline` (awk--io), and the regexes
  (`~`, `!~`, a `/re/` pattern, a regex FS) and `RS = ""`, report
  ``... is not implemented yet`` for now.
- An `AwkFatal` unwinds to `Run`, which reports gawk's ``fatal:`` line --
  with a location ``awk: <source>:<line>: (FILENAME=<f> FNR=<n>) fatal:
  <message>``, the FILENAME/FNR part only past the first record, without
  one ``awk: fatal: <message>`` -- and exits 2. A stop asked for unwinds
  as `Stopped` and exits 143; loops and the main loop check
  `ThrowIfStopped` per iteration, the record reader per read, so a
  stopped awk ends promptly.

## Documented exceptions (checked against gawk --posix 5.2.1)

- A usage error prints `awk: <error>`, then gawk's two `Usage:` lines, and
  nothing else -- gawk prints its whole option summary after them, and words
  a missing argument without quotes, or prints nothing for an unknown
  option; the option summary is `awk --help`.
- `@` (gawk's indirect-call operator) is reported as ``invalid char '@' in
  expression`` where gawk reports a syntax error at the next token.
- A backslash-newline inside a string is a continuation where gawk
  `--posix` refuses a physical newline there (`POSIX does not allow physical
  newlines in string values`).
- `break` and `continue` outside a loop are reported once; gawk prints the
  message twice.
- A function name used as its own parameter is reported once, without
  gawk's second, location-less line.
- The `(END OF FILE)` report's caret is always at column 0; gawk's column
  varies with how the `-f` file ends.
- `for (k in a)` visits the keys in insertion order; gawk's order is
  unspecified.
- A `*` width or precision in CONVFMT or OFMT counts as absent; gawk stops
  with a fatal error.
- A strnum NaN compares unordered (`kAwkUnordered`): `$1 == $2` is false for
  the fields `nan` and `+nan`; gawk 5.2.1 `--posix` gives it true.
- At most 1000000 fields may be made by an assignment (`$n = v`, `NF = n`,
  the message ``NF set to N: more than 1000000 fields``); gawk's own limit
  depends on its build.
- The parts that do not run yet (regexes, functions, `printf`, `getline`,
  output redirections) report ``... is not implemented yet``; later tasks
  of the rock append their exceptions here.