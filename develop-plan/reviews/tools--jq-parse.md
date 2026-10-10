# Review: tools--jq-parse (PR #85)
- Verdict: merged
- Merged as: 370239d, version 0.5.43
- Tokens: claude 350177, ollama input 622186, ollama output 279758

Clean-room re-check before the task (Opus, in the Claude count). Clean-room check: no jq/gojq/jaq internal names; one test comment wording ("ObjPipe") reworded. Behaviour checked on the host's jq 1.7.1 (~60 programs).

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in 09a0c77 | src/components/BuiltinCommands/commands/jq/JqParser.cpp:624 | string interpolations `"\(`, call arguments `f(` and negations in an object value had no depth guard: unbounded recursion, stack overflow on deep input (plan: 256 levels); guards, DeepNestingIsRefused cases |
| medium | fixed in 09a0c77 | tests/unit/components/Jq.unittests/JqParserTest.cpp:122 | a test comment named the object value "ObjPipe" (reads as a grammar-rule name, not Haisos's own); reworded |
| medium | commented | src/components/BuiltinCommands/commands/jq/JqAst.h | the tree's depth has no limit: long chains (`1,1,...`, `.a.a...`, `1???...`) add a level per byte or two; ~Node, DumpNode, CloneNode and the evaluator recurse (Windows: 1 MB stacks) |
| medium | commented | src/components/BuiltinCommands/commands/jq/JqParser.cpp:1146 | an identifier/keyword key in an object pattern (`. as {b: $c}`) is a Literal, object entries use String: `{null: $a}`/`{true: $a}` keys ambiguous to the evaluator |
| medium | commented | src/components/BuiltinCommands/commands/jq/JqParser.cpp:462 | three "expecting" lists differ from jq 1.7.1 (`reduce . $x`, `foreach . as $x (0 1)`, `f(1 2)`) |
| low | commented | src/components/BuiltinCommands/commands/jq/JqParser.cpp:442 | each `as` binding holds a depth level over its body: 257 chained bindings refused though not nested |
| low | commented | tests/unit/components/Jq.unittests/CMakeLists.txt:3 | no final newline |
