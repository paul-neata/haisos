# Review: coreutils--tr-tee-nl (PR #56)
- Verdict: merged
- Merged as: e488b55, version 0.5.14
- Tokens: claude 198202, ollama input 241535, ollama output 197618

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in 9039074 | src/components/BuiltinCommands/commands/tee/Tee.cpp:216 | tee stopped reading after losing its last output only in the nopipe modes; in the default and warn modes it read endless input forever (`yes \| tee --output-error=warn \| head -1` hung); now stops in every mode, as the plan and GNU; test TeeStopsOnceNoOutputIsLeft |
| high | fixed in 9039074 | src/components/BuiltinCommands/commands/nl/Nl.cpp:129 | The overflow check `INTMAX_MAX - m_increment` was signed-overflow UB for a negative `-i`, and running past INTMAX_MIN was never caught; now checked both ways, tests from GNU 9.4's output |
| high | fixed in 9039074 | src/components/BuiltinCommands/commands/nl/Nl.cpp:116 | `char formatted[32]` truncated the number for `-w` of 32 or more; now sized to the width, test for `-w 33` |
| medium | commented | src/components/BuiltinCommands/commands/tr/Tr.cpp:225 | `[=c=]` takes its operand without resolving escapes, so `tr '[=\n=]' x` fails; GNU accepts it |
| medium | commented | tests/unit/components/BuiltinCommands.unittests/TrTest.cpp | The plan's `'[=ab=]' x` error case has no test |
| low | commented | src/components/BuiltinCommands/commands/nl/Nl.cpp:143 | Members declared in a different order than initialised (-Wreorder) |
| low | commented | nl/Nl.cpp:192, tee/Tee.cpp:48 | The --help summaries are GNU's sentences rather than the plan's, and nl's notes miss the "Default options are: ..." line |
