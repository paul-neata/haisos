# Unicode

UTF-8 decoding, character classes and display widths: one compact,
dependency-free stand-in for glibc's `mbrtowc`/`iswprint`/`iswspace`/`wcwidth`
in a UTF-8 locale (`C.UTF-8`), giving every part of Haisos the same answers on
Linux, Windows and WASM alike, whatever the host locale. Free functions in
namespace `Haisos::Unicode`, no interface in `interfaces/`, no `Create()` --
a utility library like `libheaders`, but compiled (`Unicode` CMake target).

## API (`Unicode.h`)

- `DecodeUtf8(data, size)` -- strictly decodes the one UTF-8 character at the
  start of the data (RFC 3629: overlong forms, surrogates, code points above
  U+10FFFF, stray continuation bytes and cut-short sequences are all
  `Invalid` with length 1, so the caller skips that one byte and decodes
  again; `Incomplete` only when every byte present is a valid prefix and the
  data ends).
- `IsPrintable(c)`, `IsSpace(c)`, `IsNoBreakSpace(c)` -- glibc's `iswprint`
  and `iswspace`, plus the no-break spaces (U+00A0, U+2007, U+202F, U+2060)
  GNU `wc` also treats as word separators.
- `DisplayWidth(c)` -- `wcwidth`: -1 not printable, 0 combining/format, 2
  East Asian wide/fullwidth and emoji, 1 otherwise. The width tables are
  sorted, disjoint range arrays (`UnicodeTables.cpp`, declared in the
  internal `UnicodeTables.h` so the unit tests can check them) searched by
  binary search; zero-width wins where the two overlap.

## Documented approximations

Checked against glibc 2.39 in `C.UTF-8`, knowingly approximate where a full
Unicode database would cost much more than it buys:

- Unassigned code points count as printable (glibc says not).
- Combining marks of rarer scripts count width 1 -- the zero-width table
  covers the common ones, not every combining mark.
- Widths follow the fixed tables in `UnicodeTables.cpp`, not any one Unicode
  version's East Asian Width data.

## Who uses it

First the `wc` builtin (`-m`, `-w`, `-L`), whose
`--help` notes the approximations. The rule: a builtin (or anything else)
needing character classes or display widths uses this component rather than
`<cwctype>`/`wcwidth` -- those depend on the process locale, which Haisos
does not set, and `wcwidth` does not exist on Windows.
