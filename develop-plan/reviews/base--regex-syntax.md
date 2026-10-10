# Review: base--regex-syntax (PR #44)
- Verdict: merged
- Merged as: 987de46, version 0.5.2
- Tokens: claude 263605, ollama input 187235, ollama output 173245

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in 9078c25 | src/components/Regex/RegexParser.cpp:14 | The 1000-level nesting limit let the recursion use up the 1 MB Windows stack (Regex.unittests failed on Windows); lowered to 250 (PCRE2's default), with a test -- a deliberate departure from the plan's 1000 |
| high | fixed in 9078c25 | src/components/Regex/CLAUDE.md | The plan's "Notes for callers" section, which awk--records reads, was missing; added |
| high | fixed in 9078c25 | src/components/Regex/RegexParser.cpp:1113 | A GNU interval the pattern ends inside (`a\{1x`, `a{1,2`) gave "Invalid content of \{\}" instead of the plan's and glibc's "Unmatched \{"; fixed, with tests |
| medium | fixed in 9078c25 | src/components/Regex/CLAUDE.md:17 | Said `multiline` applies to Perl only, contradicting Regex.h and the code |
| low | fixed in 9078c25 | src/components/Regex/RegexParser.cpp:1123 | glibc checks m > n before the size limit (`a\{32768,1\}`); fixed, with a test |
| medium | commented | src/components/Regex/Regex.h:30 | The plan's "Semantics" doc comments are missing from the members (base--regex-match touches Regex.h and could add them) |
| medium | commented | src/components/Regex/RegexParser.cpp:576 | Duplicate Perl group names are accepted; PCRE2 refuses them |
| medium | commented | src/components/Regex/RegexParser.cpp:389 | `(?:^)*` and `(?:\b)?` are refused as not repeatable; PCRE2 accepts them |
| low | commented | src/components/Regex/RegexParser.cpp:919,931 | The Perl `[:name:]` scan runs past `]`, so `[[:a]b:]]` is wrongly refused |
| low | commented | src/components/Regex/RegexParser.cpp:889 | `[=a=]` is accepted as a range endpoint; glibc says "Invalid range end" |
| low | commented | src/components/Regex/RegexParser.cpp:763 | Perl `\10` and up are read as `\1` followed by `0` |
