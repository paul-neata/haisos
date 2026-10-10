# Task tools--jq-command: the jq builtin -- options, inputs, outputs, exit codes

- Rock: tools
- Depends on: tools--jq-paths, coreutils--sort (`BuiltinText.h`), search--rg-search (`BuiltinHelp::referenceUrl`)
- Size: ~950 changed lines in ~9 files
- Plan checked against: develop @ ccb9dbe
- PR title: Add the jq builtin: options, inputs, output formats, exit codes

## Goal

The fifth jq task: `jq` is a builtin command (`BUILTIN rootfs jq /bin/jq`)
running the evaluator of the earlier jq tasks, with jq 1.7.1's command
line: every option of `jq --help` accepted, the filter from the command
line or `-f`, inputs from files or standard input (several JSON values,
`-n`, `-s`, `-R`), `--arg`/`--argjson`/`--slurpfile`/`--rawfile`/`--args`/
`--jsonargs`/`$ARGS`/`$ENV`, the output options (`-r -j -a -c -C -M -S
--tab --indent n --raw-output0 --seq`), `-e`, and jq's messages and exit
codes byte for byte. The I/O builtins come with it: `input`, `inputs`,
`debug`, `debug(msg)`, `stderr`, `input_filename`, `input_line_number`,
`env`, `halt`, `halt_error`. After this task `cat package.json | jq
.version` and `jq -r '.dependencies | keys[]' package.json` work (`keys`
arrives with `tools--jq-builtins`).

Reference: the task container's jq (Ubuntu 24.04's 1.7.1 package, `jq
--version` printing `jq-1.7`); every expected output here is its. For any
case not written out, run the same command there and copy the output.

## Context

Read first: the root `CLAUDE.md` (Security, Builtin Commands, rule 9),
`src/components/BuiltinCommands/CLAUDE.md`, `commands/jq/CLAUDE.md`,
`commands/wc/Wc.cpp` (a model builtin: reading descriptors, stop and error
handling). From earlier tasks, by exact name:
- `tools--jq-parse`: `ParseProgram`, `CompileError`, `FormatCompileError`,
  `FormatCompileErrorCount`.
- `tools--jq-json`: `Value`, `WriteJson`, `WriteOptions { indent, tab,
  sortKeys, ascii, color }`, `JsonReader` (`Feed`, `Finish`),
  `ParseSingleJson`, `RepairUtf8`.
- `tools--jq-eval`: `Program::Compile(text, globals, errors)`,
  `Program::Run(input, globals, host, emit)`, `JqHost` (`NextInput`,
  `WriteStderr`, `InputFilename`, `InputLineNumber`, `StopRequested`),
  `JqError`, `JqStopped`, `NativeRegistry` (`Add`, `Standard()`),
  `NativeFunction`, `ForEachArgumentCombination`, `Interpreter::Host()`.
- `tools--jq-paths`: paths and assignment (nothing to call directly).
- `coreutils--sort`: `BuiltinText.h` -- `OpenInputOperand(context, name,
  failure)` with `InputOpenFailure`, `BuiltinLineReader` (`Next(line,
  delimited)`, `LineReadResult`).
- `search--rg-search`: `BuiltinHelp::referenceUrl` (printed on the "Based
  on" line instead of the man7 URL when set). That task also adapted
  `EveryBuiltinsHelpHasTheSameShape` to it; if it did not, make the test
  expect `referenceUrl` when it is set.
- `coreutils--uniq-cut`: `BuiltinOption::hidden` (parsed, not shown in --help).

Rules that bite (root `CLAUDE.md`): `ICurrentProcess` is the only door --
files through `context.IO()`, the environment through
`context.Process().GetEnvironment()`; the evaluator reaches them only
through the `JqHost` this task implements. Builtin rules: jq's output byte
for byte; every jq 1.7.1 option in `Options()` (untreated ones
`kBuiltinNotTreated`, reported); `--help` from `BuiltinHelpText`;
`--version`; default `ManPage()`; registered in
`CreateStandardBuiltinCommands()` (which puts `# BUILTIN rootfs jq /bin/jq`
in the `haisos --init` template -- never hand-written); sources in
`CMakeLists.txt`; `jq` added to `ListsEveryBuiltinSortedWithAVersion`'s
exact list. Reading stops promptly on `TriggerStop()` (exit 143 is the
process's business; return at once); a broken stdout pipe is handled by
`BuiltinContext` (141). Portable C++17.

## Changes

### `commands/jq/Jq.cpp` (new) -- the command

`CreateJqCommand()` (declared in `BuiltinCommandList.h`, added to
`CreateStandardBuiltinCommands()` in alphabetical place). Name `jq`,
version `1.0.0`. `Help()`: summary `command-line JSON processor`; usage
`jq [OPTION]... FILTER [FILE]...`, `jq [OPTION]... --args FILTER
[STRING]...`, `jq [OPTION]... --jsonargs FILTER [JSON_TEXT]...`;
`referenceUrl` `https://jqlang.github.io/jq/manual/v1.7/`; notes (two or
three lines): the jq 1.7 language without modules; numbers keep their
literal text as jq 1.7 does; regular expressions use Haisos's Perl subset
(from `tools--jq-text`); function calls nest at most 512 deep.

Option table (every option of `jq --help` 1.7.1, in its order; `id`
private constants; treated ones with a few words):

| short | long | argument | |
|---|---|---|---|
| n | null-input | | treated |
| R | raw-input | | treated |
| s | slurp | | treated |
| c | compact-output | | treated |
| r | raw-output | | treated |
| | raw-output0 | | treated |
| j | join-output | | treated |
| a | ascii-output | | treated |
| S | sort-keys | | treated |
| C | color-output | | treated |
| M | monochrome-output | | treated |
| | tab | | treated |
| | indent | Required n | treated |
| | unbuffered | | treated (flush after each output) |
| | stream | | not treated |
| | stream-errors | | not treated |
| | seq | | treated (output only: see below) |
| f | from-file | Required file | treated |
| L | | Required directory | not treated (no modules) |
| | arg | Required name (+ value) | treated |
| | argjson | Required name (+ text) | treated |
| | slurpfile | Required name (+ file) | treated |
| | rawfile | Required name (+ file) | treated |
| | args | | treated |
| | jsonargs | | treated |
| e | exit-status | | treated |
| | build-configuration | | not treated |
| | debug-dump-disasm | | not treated, hidden |
| | debug-trace | | not treated, hidden |

`-h` and `-V` are `--help` and `--version` (`kBuiltinOptionHelp`,
`kBuiltinOptionVersion` rows, as rg's plan does). The table drives
`--help` and the not-treated reports; the arguments themselves are parsed
by `JqArguments`, not `ParseBuiltinArgs` (jq's parser is not getopt's: two-
argument options, exact long names, `--args` modes).

`Run`, in order:
1. `ParseJqArguments` (below). A usage error prints its message and the
   two jq lines, exit 2:
   `jq: Unknown option --foo\nUse jq --help for help with command-line options,\nor see the jq manpage, or online docs  at https://jqlang.github.io/jq\n`
   (two spaces before `at`, as jq). `--help`/`-h` anywhere: `BuiltinHelpText`
   to stdout, exit 0; `--version`/`-V`: `BuiltinVersionText`, exit 0.
   Each not-treated option given: `context.NotTreated(spelling)` (once
   each), then go on as if it was absent.
2. The filter: `-f FILE` reads the program from FILE through
   `context.IO()` (missing: `jq: Could not open FILE: No such file or
   directory`, exit 2); else the first operand. No filter at all: when both
   stdin and stdout are terminals (`IFileDescriptor::IsTerminal` of slots 0
   and 1), print jq's short usage to stderr and exit 2 -- the text of `jq`
   run bare on a terminal in the container, with `[version 1.7]` replaced
   by `[HaisosOS builtin 1.0.0]` (a documented exception); otherwise the
   filter is `.`.
3. Globals: `ENV` (an object of the process environment's variables, in
   `GetVariableNames()` order -- secrets are not variables and never appear),
   `__loc__` is not a global (the parser's), each named argument, and `ARGS`
   = `{"positional": [...], "named": {...}}` (named in the order given:
   `$ARGS` with `--arg a 1 --argjson b 2` is
   `{"positional":[],"named":{"a":"1","b":2}}`). `Program::Compile` with
   their names; compile errors: each `FormatCompileError`, then
   `FormatCompileErrorCount`, to stderr, exit 3.
4. Inputs (`JqInput`, below). For each input value (or `null` once with
   `-n`, or the slurped array/string once with `-s`), `Program::Run` with
   an emit that prints the result (step 5); an uncaught `JqError` prints
   `jq: error (at <location>): <message>\n` -- `<message>` the string as is
   (up to a NUL), or for a non-string `jq: error (at <location>) (not a
   string): <compact JSON>\n` -- marks the exit status 5 and goes on with
   the next input. `<location>` is `<file>:<line>` of the current input
   (`<stdin>` for standard input; the line is the number of `\n` fed from
   that file so far, see JqInput), or `<unknown>` before any input was read
   (`jq -n '1|error'` -> `jq: error (at <unknown>) (not a string): 1`).
   `JqStopped`: return at once.
5. Printing one result: with `-r`/`-j`/`--raw-output0` a string is written
   raw (with `-a`, its non-ASCII code points escaped as in JSON but without
   quotes -- verify: `jq -ran '"é"'`), anything else with `WriteJson`;
   then `\n`, nothing with `-j`, or `\0` with `--raw-output0` (a string
   holding NUL then fails: `jq: error (at <unknown>): Cannot dump a string
   containing NUL with --raw-output0 option`, exit 5). With `--seq` each
   result is preceded by `\x1e` (`jq -n --seq '[1]'` is
   `\x1e[\n  1\n]\n`). `--unbuffered`: `context.Flush()` after each.
   `WriteOptions`: indent 2; `--tab`, `--indent n` and `-c` each override
   the earlier ones, the last given wins (`--tab -c` compact, `-c --tab`
   tabs, `--indent 3 --tab` tabs, `--tab --indent 3` three spaces);
   `--indent 0` is compact, `--indent -1` is tabs; outside -1..7: `jq:
   --indent takes a number between -1 and 7` + the two lines, exit 2.
   Colour: on when stdout is a terminal (`context.OutIsTerminal()`) and the
   environment's `NO_COLOR` is unset or empty; `-C` forces it on, `-M` off,
   the last of the two wins.
6. Exit status: 2 if a file could not be opened (this wins over 5), else 5
   after any uncaught error or input parse error, else with `-e`: 1 when the
   last output was `null` or `false`, 4 when there was no output at all,
   else 0; without `-e`, 0. `halt` exits 0 at once; `halt_error`/
   `halt_error(code)` (natives below) exit with that code.

### `commands/jq/JqArguments.h` / `.cpp` (new)

```cpp
struct JqNamedArgument { std::string name; Value value; };
struct JqArguments {
    std::optional<std::string> filter;          // null with -f before it is read
    std::optional<std::string> filterFile;      // -f
    std::vector<std::string> files;
    std::vector<Value> positional;              // --args / --jsonargs
    std::vector<JqNamedArgument> named;
    bool nullInput = false, rawInput = false, slurp = false, rawOutput = false,
         joinOutput = false, rawOutput0 = false, ascii = false, sortKeys = false,
         exitStatus = false, seq = false, unbuffered = false;
    std::optional<bool> color;                  // -C / -M, last wins
    WriteOptions layout;                        // indent / tab / compact, last wins
    std::vector<std::string> notTreated;        // spellings given
    bool help = false, version = false;
    std::string usageError;                     // the first line, e.g. "Unknown option --foo"
};
JqArguments ParseJqArguments(BuiltinContext& context, const std::vector<std::string>& args);
```
jq 1.7.1's loop: after `--`, every word is a non-option. A non-option is
the filter if none yet (and no `-f`), else a positional argument when
`--args`/`--jsonargs` mode is on (the last of the two given decides how it
is read; `--jsonargs` text must be one JSON value, else `jq: invalid JSON
text passed to --jsonargs` + the two lines, exit 2), else an input file. A
word `-` followed by a digit (`-5`) is not an option (jq takes it as the
filter); a lone `-` is not an option either (verify what jq makes of it).
Short options cluster (`-nr`, `-sR`); unknown letters: `Unknown option
-x`. Long options must be spelled whole (`--null` is `Unknown option
--null`). Missing parameters: `--arg takes two parameters (e.g. --arg
varname value)`, `--argjson takes two parameters (e.g. --argjson varname
text)`, `--slurpfile takes two parameters (e.g. --slurpfile varname
filename)`, `--rawfile` likewise, `--indent takes one parameter` (verify
the last three in the container); each printed as `jq: <text>` + the two
lines, exit 2. `--argjson` with bad text: `jq: invalid JSON text passed to
--argjson`. `--slurpfile`/`--rawfile`: the file read through
`context.IO()` now (all its values as an array, with `JsonReader`; raw
bytes through `RepairUtf8`); failure: `jq: Bad JSON in --slurpfile x
nofile: Could not open nofile: No such file or directory` (`--rawfile` the
same words), exit 2. `--arg` values pass through `RepairUtf8`.

### `commands/jq/JqInput.h` / `.cpp` (new) -- the inputs and the host

```cpp
class JqInput : public JqHost {
public:
    JqInput(BuiltinContext& context, const JqArguments& arguments);
    std::optional<Value> NextInput() override;     // also what the main loop uses
    void WriteStderr(const std::string& bytes) override;   // context.ErrorText
    Value InputFilename() const override;          // the current file's name, or null for stdin / none yet
    int InputLineNumber() const override;
    bool StopRequested() const override;           // context.StopRequested()
    std::string Location() const;                  // "<stdin>:3", "in.json:2", "<unknown>"
    bool OpenFailed() const;                       // a file could not be opened
    bool ParseFailed() const;
    bool Stopped() const;
};
```
- Files in order (none: stdin, `-`), each opened with `OpenInputOperand`;
  missing: `jq: error: Could not open file NAME: No such file or
  directory` (the rest still read; exit 2 at the end); a directory: nothing
  printed, exit 2 (the container's jq prints nothing for `jq . /tmp` and
  exits 2 -- verify); other failures `Permission denied` the same way.
- Each file is read in lines (`BuiltinLineReader` with `'\n'`, the `\n`
  put back) and each line fed to **one** `JsonReader` shared by all files
  (jq parses the files as one stream: an unfinished value in one file runs
  on into the next, and a parse error's line counts across files). The line
  counter of the *current file* (`\n`s fed from it) is the `<line>` of
  `Location()` for every value completed by that feed, and the input
  line number. After the last file, `Finish`.
- A parse error: `jq: parse error: <message>\n` on stderr (after the
  values before it were processed), no further input, exit 5.
- `-R`: each line (without its `\n`, through `RepairUtf8`; a last line
  without `\n` counts) is a string input. `-R -s`: the whole text of every
  file concatenated, one string. `-s`: one array of every value (empty
  input: `[]`). `-n`: the main loop runs once on `null`; `input`/`inputs`
  still read the files.

### `commands/jq/JqIoNatives.cpp` (new)

`RegisterIoNatives(NativeRegistry&)`, called from `NativeRegistry::Standard()`
(declared in `JqNatives.h`). Every one uses `interp.Host()`:
- `input`: the next input, or the error `No more inputs` (verify the exact
  text in the container with an empty file: `jq -n input /dev/null`).
- `inputs`: every remaining input.
- `debug`: writes `["DEBUG:",<input compact>]\n` to stderr, outputs its
  input; `debug(msg)`: for each output m of msg, `["DEBUG:",m]\n`, then the
  input once (jq 1.7.1: `def debug(msg): (msg | debug | empty), .;`).
- `stderr`: writes the input to stderr -- a string raw, anything else
  compact JSON, no newline (jq 1.7.1) -- and outputs it.
- `input_filename`, `input_line_number`: from the host.
- `env`: `$ENV` (define it in the prelude as `def env: $ENV;`).
- `halt`: throws `JqHalt { int code = 0; std::optional<std::string> stderrText; }`
  (declare it in `JqRuntime.h`); `halt_error`: `halt_error(5)`;
  `halt_error(code)`: a string input written raw to stderr (no newline
  added), anything else as compact JSON plus `\n`, then exit with `code`
  (`"bye\n"|halt_error(1)` prints `bye` and a newline, exit 1; `{} |
  halt_error` prints `{}` and a newline, exit 5); a non-number code fails
  `halt_error/1: number required` (verify). The command catches `JqHalt`,
  flushes stdout, writes the text, returns the code.

### Registration and build

- `BuiltinCommandList.h`: `CreateJqCommand()` declared and added.
- `src/components/BuiltinCommands/CMakeLists.txt`: `commands/jq/Jq.cpp`,
  `commands/jq/JqArguments.cpp`, `commands/jq/JqInput.cpp`,
  `commands/jq/JqIoNatives.cpp`.

## Tests

New `tests/unit/components/BuiltinCommands.unittests/JqTest.cpp` (in that
directory's CMakeLists), `TEST_F(BuiltinCommandsTest, Jq...)`, through
`RunCaptured("jq", args, input)` (stdout is not a terminal: no colour by
default); files written with `WriteFile`. Expected texts are the
container's:
- `JqIdentityPrettyPrints`: input `{"a":[1,"x",null,true,{}],"b":{}}` ->
  the pretty text; `-c` compact; `--tab`; `--indent 1`; `-S`.
- `JqSeveralValuesAndFiles`: files `/w/in.json` = `{"a":1}\n{"a":2}\n` and
  `/w/one.json` = `[1]\n`; `jq -c .a in.json one.json` -> `1\n2\n` on
  stdout and `jq: error (at one.json:1): Cannot index array with string "a"\n`
  on stderr, exit 5.
- `JqErrorLocations`: stdin `{"a":1}\n\n\n{"a":2}` with `.a.b` -> errors at
  `<stdin>:1` and `<stdin>:3`; stdin `1 2 3` with `error` -> three `(at
  <stdin>:0) (not a string): N` lines; `-n '1|error'` -> `(at <unknown>)`.
- `JqRawAndJoin`: `-r` `"a\tb"` -> `a\tb\n`; `-j` `"a", 1` -> `a1`;
  `--raw-output0` `"a","b"` -> `a\0b\0`; the NUL error.
- `JqNullInputSlurpRaw`: `-n`; `-s` of two values; `-n '[inputs]' in.json`;
  `-n input in.json`; `-R .` of `x\ny\n` -> `"x"\n"y"\n`; `-Rs .` -> `"x\ny\n"\n`;
  `-sR . t.txt in.json` concatenated.
- `JqNamedArguments`: `--arg x 1 '$x'` -> `"1"`; `--argjson`; `--slurpfile`;
  `--rawfile`; `$ARGS` with `--args a b` and `--jsonargs 1 '{}'`; `$ARGS`
  with named ones; `--args x -- -y`.
- `JqExitStatus`: `-e .b` -> outputs `null` twice, exit 1; `-en false` 1;
  `-en empty` 4; `-en 'null, 1'` 0; `-en '1, null'` 1; a runtime error 5;
  a compile error 3; a missing file 2 (`jq: error: Could not open file
  nofile: No such file or directory`, other files still processed).
- `JqParseErrors`: `[1,2\n` -> `jq: parse error: Unfinished JSON term at
  EOF at line 2, column 0\n`, exit 5; values before the error printed;
  the across-files line count (`{"a":1}\n{` then in.json ->
  `Expected separator between values at line 3, column 1`).
- `JqCompileErrors`: `.a.b)` -> the full three-line text, exit 3; `foo`.
- `JqUsageErrors`: `--foo` and `-x` -> the three lines, exit 2; `--arg x`;
  `--indent 8`; `--argjson x '{a'`; `-f nofile`.
- `JqColour`: `-C '{"a":null}'` with `-c` -> the byte string of
  `tools--jq-json`'s plan; `-C -M` none; a terminal stdout: run with the
  fixture's terminal mode if it has one, else skip -- `NO_COLOR` set
  disables it.
- `JqIoBuiltins`: `"a" | debug, stderr` -> stdout `"a"\n"a"\n`, stderr
  `["DEBUG:","a"]\na`; `debug("msg: \(.a)")`; `input_filename` with
  files; `$ENV.HOME|type` with an environment holding HOME;
  `env|type`; `halt` (exit 0, nothing after); `"bye\n"|halt_error(1)`;
  `{}|halt_error`.
- `JqDefaultFilter`: no filter with stdin not a terminal -> acts as `.`.
- `JqHelpAndVersion`: `--help` starts `HaisosOS jq version 1.0.0 -
  command-line JSON processor` and its second line is `Based on Linux jq:
  https://jqlang.github.io/jq/manual/v1.7/`; `-h` same; `-V` prints
  `jq (HaisosOS builtin) 1.0.0`; `-L x .` reports `Parameter -L is not
  treated by HaisosOS jq v. 1.0.0`.
- Update `ListsEveryBuiltinSortedWithAVersion` (insert `"jq"` in sorted
  place in the list as it is on develop). The generic tests
  (`EveryBuiltinsHelpHasTheSameShape`, `EveryBuiltinsManPageIsItsHelp`,
  `EveryUntreatedOptionIsAcceptedAndReported` -- it runs e.g. `jq --stream
  /docs`: the report line must appear even though the filter `/docs` then
  fails to compile) and `TheInitTemplatesBuiltinsAllApplyOnceUncommented`
  cover the rest.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Jq*:BuiltinCommandsTest.Every*:BuiltinCommandsTest.Lists*'
bash ./scripts/test_linux.sh L U Jq
bash ./scripts/test_linux.sh L U CliParser
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: `jq` in the opening list; a
  row in "The commands" table (version 1.0.0; treated: every option but
  those below, inputs from files or stdin, jq 1.7.1's output, messages and
  exit codes; exceptions: no modules (`-L` not treated), `--stream`,
  `--stream-errors`, `--build-configuration` not treated, `--seq` affects
  output only, the bare-terminal usage names the builtin's version, calls
  nest at most 512 deep, `JQ_COLORS` is not read).
- Root `CLAUDE.md`: `jq` in the Builtin Commands sentence and table
  (`A jq 1.7 subset: filters, paths, assignment, the builtin library;
  jq's options, output and messages`), and in the `BuiltinCommands/` line
  of the directory tree.
- `commands/jq/CLAUDE.md`: the command's files and the I/O natives.

## Acceptance

- [ ] `jq` registered; `--help` shape with the jq manual URL; `--version`;
      `man jq`; in the `--init` template (generated).
- [ ] Every jq 1.7.1 option accepted; untreated ones reported.
- [ ] Inputs, `-n -s -R`, named and positional arguments, output options,
      colours, `-e` and every exit status as the container's jq.
- [ ] Error locations (`<file>:<line>`, `<unknown>`) and every message
      listed byte-exact.
- [ ] Every file and the environment reached only through `context`;
      reading stops on `StopRequested`.
- [ ] Builds; `BuiltinCommands.unittests`, `Jq.unittests` and
      `CliParser.unittests` pass.

## Out of scope

- The builtin library (`length`, `keys`, `map`, ...): `tools--jq-builtins`;
  regex, formats, dates: `tools--jq-text`.
- `--stream`, `--stream-errors`, modules, `JQ_COLORS`, `--seq` input parsing.
