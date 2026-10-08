# Review: base--regex-match (PR #45)
- Verdict: merged
- Merged as: 4ec5ed8, version 0.5.3
- Tokens: claude 203734, ollama input 115519, ollama output 284991

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| critical | fixed in 463ee44 | tests/unit/components/Regex.unittests/RegexMatchTest.cpp:236 | LongLinesDoNotOverflowTheStack took 10.4-10.9 s, over its 10 s limit (`(a?){30000}` on 1M a's); that case now runs on 3000 a's |
| high | fixed in 1e8079a | src/components/Regex/RegexProgram.cpp:34 | `\b` `\B` `\<` `\>` at position 0 treated text[0] as the byte before it, so `\bfoo` did not match "foo" |
| high | fixed in 1e8079a | src/components/Regex/RegexProgram.cpp:187 | NodeSize of an unbounded repeat left out its body, so `((a{1000}){1000})*` got past the size limit (memory exhaustion) |
| high | fixed in 1e8079a | src/components/Regex/RegexProgram.cpp:64 | Compiler recursion unbounded for stacked GNU quantifiers (`a**...*`), overflowing the stack; an iterative depth check now refuses trees deeper than 1100 |
| high | fixed in 1e8079a | src/components/Regex/RegexPikeVM.cpp:196 | The closure stack used reserve() then operator[] past size(): undefined behaviour (fails in MSVC debug builds); now resize() |
| medium | commented | src/components/Regex/CLAUDE.md:137 | The documented GNU difference uses `\(a\*\)*\1b` (a literal star); host GNU sed does match `\(a*\)*\1b` on "b" |
| medium | commented | src/components/Regex/RegexPikeVM.cpp:83 | Each thread keeps its own copy of the capture slots: memory grows as threads x groups (GBs with thousands of groups) |
| medium | commented | src/components/Regex/CLAUDE.md:87 | The docs don't say what O(text x program) costs for large programs (about 10 s per 30000 bytes on `(a?){30000}`) |
| low | commented | tests/unit/components/Regex.unittests/RegexMatchTest.cpp:212 | When compile fails, CompileOrDie returns null and the test dereferences it |
| low | commented | tests/unit/components/Regex.unittests/RegexMatchTest.cpp:134 | The `\(\(a\?\)\+\)*b\1x` row pins a case where host glibc itself prints garbage |
| low | commented | src/components/Regex/RegexPikeVM.cpp:42 | The int generation counter wraps on texts over about 4 GB |
