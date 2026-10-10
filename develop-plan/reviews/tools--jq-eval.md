# Review: tools--jq-eval (PR #87)
- Verdict: merged
- Merged as: 6033aa3, version 0.5.45
- Tokens: claude 466402, ollama input 706664, ollama output 224653

Clean-room re-check before the task (Opus, in the Claude count). Clean-room check: no copying; the prelude's `select` has jq's text, the one obvious form of the manual's description (not counted). The MSVC test fix compiles; Windows tests still red (Jq.unittests likely a stack overflow in DepthLimit at 1024 levels; Awk/BuiltinCommands red before; HaisosOS flaky).

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in edd554e | src/components/BuiltinCommands/commands/jq/JqInterpreter.cpp:362 | a `$a` parameter called as the filter `a` returned the bound value; jq runs the argument again (`def f($a): a; [f(1,2)]` -> `[1,2,1,2]`); tests |
| high | fixed in edd554e | tests/unit/components/Jq.unittests/JqInterpreterTest.cpp:375 | "300 chained bindings" tested with 200, on a wrong count; now 300 |
| medium | commented | src/components/BuiltinCommands/commands/jq/JqArithmetic.cpp:119 | `"" / ","` gives `[""]` (jq `[]`); `"aé" / ""` splits bytes (jq code points) |
| medium | commented | src/components/BuiltinCommands/commands/jq/JqArithmetic.cpp:196 | `"ab" * 1e18` builds without a size limit or stop check: bad_alloc, not a JqError |
| medium | commented | src/components/BuiltinCommands/commands/jq/JqProgram.cpp:179 | `. as {a:$x, ($x):$y}` compiles; jq: `$x is not defined` |
| medium | commented | src/components/BuiltinCommands/commands/jq/JqInterpreter.cpp:239 | `?//` does not try the next alternative when the error comes after an inner try |
| low | commented | src/components/BuiltinCommands/commands/jq/JqArithmetic.cpp:54 | object merging with `+`/`*` scans linearly: quadratic |
| low | commented | src/components/BuiltinCommands/commands/jq/CLAUDE.md:209 | says an `as` binding costs "about two" levels; one |
| low | commented | src/components/BuiltinCommands/commands/jq/JqUtf8.cpp:120 | Utf8ByteOffset does not check continuation bytes |
