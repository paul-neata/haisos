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
  `AwkLocationPrefix`: `awk: <source>:<line>: `.
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
    `**=` and `|&` are not POSIX: `**` is two `*` tokens. A byte that starts
    no token is `invalid char '<c>' in expression`.
- `AwkInvocation.h/.cpp` - the command line, as gawk takes it:
  `AwkOptionTable` (every gawk option, treated or not: `-F -f -v` with their
  long names, `-h -V -P -r`, the rest marked for the not-treated report),
  `ParseAwkInvocation` (options stop at the first operand; usage errors print
  gawk's two usage lines, status 1; a `-v` value without `=` gawk's message)
  and `LoadAwkSources` (each `-f` file read whole through `OpenInputOperand`
  and `ReadWholeInput` -- `-` is standard input; an unreadable file is
  gawk's fatal, status 2, a directory its own error, status 1).
- `Awk.cpp` - the `awk` builtin itself: `Name`, `Version` 1.0.0, the option
  table, the gawk-based `--help` (`BuiltinHelp::basedOn`), and `Run`:
  parse the invocation, load the sources, then -- for now -- report `awk:
  running programs is not implemented yet` with status 2 (awk--parser
  replaces this).

The man page is the `--help` text, like most builtins (`ManPage` is not
overridden).

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
- Running programs is not implemented yet (this task is the lexer); later
  tasks of the rock append their exceptions here.