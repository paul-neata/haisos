# Review: coreutils--printf-seq (PR #53)
- Verdict: merged
- Merged as: 69f7695, version 0.5.11
- Tokens: claude 196341, ollama input 270978, ollama output 142877

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in f991a8e | src/components/BuiltinCommands/commands/seq/Seq.cpp:35 | StrtoldAll accepted an operand that overflows strtold as infinity, so `seq 1e5000` (`seq 1e309` on Windows) printed forever where GNU rejects it ("invalid floating point argument"); overflow now refused, underflow accepted, as GNU's xstrtold; test in SeqErrors |
| medium | commented | src/components/BuiltinCommands/commands/printf/Printf.cpp:34 | A character constant takes the UTF-8 code point (`'é` gives 233), where GNU in the C locale gives 195 and a warning; allowed by the plan but not documented as an exception in --help or the CLAUDE.md row |
| medium | commented | src/components/BuiltinCommands/commands/printf/Printf.cpp:324 | --help says an invalid numeric argument "is a warning, not a failure", but it sets exit status 1, as GNU does |
| low | commented | src/components/BuiltinCommands/commands/printf/Printf.cpp:294 | A `*` precision below INT_MIN is narrowed with static_cast<int> and can wrap positive |
| low | commented | src/components/BuiltinCommands/commands/printf/Printf.cpp:257 | The spec is validated before its `*` arguments are read; GNU reads them first, so a bad `*` argument's diagnostic is missing before the invalid-spec error |
| low | commented | src/components/BuiltinCommands/BuiltinPrintf.h:23 | `q` dropped from the skipped length modifiers the contract lists (the code matches GNU printf.c; the contract text, reused by the awk/find tasks, differs) |
