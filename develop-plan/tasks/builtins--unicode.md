# Task builtins--unicode: A Unicode component -- UTF-8 decoding, character classes, widths

- Rock: builtins
- Depends on: none
- Size: ~430 changed lines in ~8 files (about 150 of them tests, 100 tables)
- Plan checked against: develop @ 8fb8324
- PR title: Add the Unicode component: UTF-8 decoding, classes, widths

## Goal

A new library component, `src/components/Unicode/` (CMake target `Unicode`,
namespace `Haisos::Unicode`), gives every part of Haisos the same answers to
"what character do these UTF-8 bytes hold", "is it printable", "is it white
space" and "how many terminal columns does it take" -- on Linux, Windows and
WASM alike, whatever the host locale. It is a compact stand-in for glibc's
`mbrtowc`/`iswprint`/`iswspace`/`wcwidth` in a UTF-8 locale. Its first user
is the `wc` builtin (builtins--wc: `-m`, `-w`, `-L`), which needs exactly
these. No behaviour a user sees changes in this task.

## Context

Read first: the root `CLAUDE.md` (Directory Structure, the Architecture
component table, "Automatic Development Rules"), and as models for a small
component and its tests: `src/components/Environment/CMakeLists.txt`,
`tests/unit/components/Environment.unittests/CMakeLists.txt`,
`tests/unit/CMakeLists.txt`, the root `CMakeLists.txt` (its list of
`add_subdirectory(src/components/...)`).

Nothing from other tasks is needed. The component depends on nothing (not
even `Logger`): pure functions over bytes and code points.

Why not the C library: `<cwctype>` and `wcwidth` depend on the process
locale (Haisos does not set one; the host's may be `C`), and `wcwidth` does
not exist on Windows. The values below were checked against glibc 2.39 in
`C.UTF-8` (through GNU `wc -L`/`-w`/`-m`) when this plan was written.

The documented approximation, chosen by the user: unassigned code points
count as printable (glibc says not), and combining marks of rarer scripts
count width 1 (the zero-width table covers the common ones). `wc` states this
in its `--help` notes.

## Changes

A small, dependency-free library: no interface in `interfaces/` (it is a
utility, not a service -- like `libheaders`, but compiled), no `Create()`
(no classes implementing interfaces; free functions only). It is a compact
stand-in for glibc's `mbrtowc`/`iswprint`/`iswspace`/`wcwidth` in a UTF-8
locale, the same on Linux, Windows and WASM.

Files:

- `src/components/Unicode/Unicode.h` -- the public API, exactly:
  ```cpp
  #pragma once
  #include <cstddef>

  namespace Haisos::Unicode {

  // The largest Unicode code point.
  constexpr char32_t kMaxCodePoint = 0x10FFFF;

  enum class DecodeStatus {
      Ok,          // a whole, valid character
      Invalid,     // the first byte starts no valid sequence: skip it alone
      Incomplete,  // a valid start, but the data ends before the sequence does
  };

  struct DecodedChar {
      DecodeStatus status = DecodeStatus::Invalid;
      char32_t codePoint = 0;  // meaningful only when status is Ok
      size_t length = 0;       // bytes taken: the sequence's (Ok), 1 (Invalid), 0 (Incomplete)
  };

  // Decodes the UTF-8 character at the start of data[0..size). size must be
  // at least 1. Strict UTF-8 (RFC 3629): overlong forms, surrogates
  // (U+D800-U+DFFF), code points above U+10FFFF, stray continuation bytes,
  // C0, C1, F5-FF, and a sequence cut short by a byte that is not a
  // continuation byte are all Invalid, with length 1 -- the caller skips
  // that one byte and decodes again from the next (as glibc's mbrtowc
  // returning -1). Incomplete only when every byte present is a valid
  // prefix and the data simply ends.
  DecodedChar DecodeUtf8(const char* data, size_t size);

  // Whether c is printable (glibc's iswprint in a UTF-8 locale, compactly):
  // every code point up to kMaxCodePoint except U+0000-U+001F,
  // U+007F-U+009F, U+2028, U+2029, U+FDD0-U+FDEF, U+D800-U+DFFF and
  // U+xxFFFE/U+xxFFFF of every plane. Unassigned code points count as
  // printable (glibc says not).
  bool IsPrintable(char32_t c);

  // Whether c is white space (glibc's iswspace): U+0009-U+000D, U+0020,
  // U+1680, U+2000-U+2006, U+2008-U+200A, U+2028, U+2029, U+205F, U+3000.
  bool IsSpace(char32_t c);

  // Whether c is one of the no-break spaces GNU wc also treats as word
  // separators: U+00A0, U+2007, U+202F, U+2060.
  bool IsNoBreakSpace(char32_t c);

  // Columns c takes on a terminal (wcwidth): -1 if it is not printable
  // (IsPrintable false), 0 for combining and format characters, 2 for East
  // Asian wide and fullwidth characters and emoji, 1 otherwise. A compact
  // table: rarer scripts' combining marks count 1.
  int DisplayWidth(char32_t c);

  } // namespace Haisos::Unicode
  ```
- `src/components/Unicode/Unicode.cpp` -- the decoder and the four
  predicates. The decoder, byte by byte: `00-7F` -> 1 byte; `C2-DF` -> 2;
  `E0` (second byte `A0-BF`), `E1-EC`, `EE-EF` (`80-BF`), `ED` (second byte
  `80-9F`) -> 3; `F0` (second `90-BF`), `F1-F3` (`80-BF`), `F4` (second
  `80-8F`) -> 4; every later byte `80-BF`; anything else Invalid. A
  mismatch at any position -> Invalid, length 1.
- `src/components/Unicode/UnicodeTables.cpp` -- the width tables, as sorted
  `{first, last}` range arrays searched by binary search (internal linkage,
  or declared in an internal `UnicodeTables.h`):
  - **width 0** (when printable): U+0300-036F, U+0483-0489, U+0591-05BD,
    U+05BF, U+05C1-05C2, U+05C4-05C5, U+05C7, U+0610-061A, U+061C,
    U+064B-065F, U+0670, U+06D6-06DC, U+06DF-06E4, U+06E7-06E8, U+06EA-06ED,
    U+0900-0902, U+093A, U+093C, U+0941-0948, U+094D, U+0951-0957,
    U+0962-0963, U+1160-11FF, U+1AB0-1AFF, U+1DC0-1DFF, U+200B-200F,
    U+202A-202E, U+2060-2064, U+2066-206F, U+20D0-20FF, U+302A-302D,
    U+3099-309A, U+FE00-FE0F, U+FE20-FE2F, U+FEFF, U+E0001, U+E0020-E007F,
    U+E0100-E01EF;
  - **width 2**: U+1100-115F, U+231A-231B, U+2329-232A, U+23E9-23EC, U+23F0,
    U+23F3, U+25FD-25FE, U+2614-2615, U+2648-2653, U+267F, U+2693, U+26A1,
    U+26AA-26AB, U+26BD-26BE, U+26C4-26C5, U+26CE, U+26D4, U+26EA,
    U+26F2-26F3, U+26F5, U+26FA, U+26FD, U+2705, U+270A-270B, U+2728, U+274C,
    U+274E, U+2753-2755, U+2757, U+2795-2797, U+27B0, U+27BF, U+2B1B-2B1C,
    U+2B50, U+2B55, U+2E80-303E (the width-0 ones above win), U+3041-33FF,
    U+3400-4DBF, U+4E00-9FFF, U+A000-A4CF, U+A960-A97F, U+AC00-D7A3,
    U+F900-FAFF, U+FE10-FE19, U+FE30-FE6F, U+FF00-FF60, U+FFE0-FFE6,
    U+16FE0-16FE4, U+17000-18CFF, U+1B000-1B2FF, U+1F004, U+1F0CF, U+1F18E,
    U+1F191-1F19A, U+1F200-1F202, U+1F210-1F23B, U+1F240-1F248,
    U+1F250-1F251, U+1F300-1F64F, U+1F680-1F6FF, U+1F7E0-1F7EB,
    U+1F900-1F9FF, U+1FA70-1FAFF, U+20000-2FFFD, U+30000-3FFFD;
  - everything else printable: 1.
- `src/components/Unicode/CMakeLists.txt`:
  `add_library(Unicode STATIC Unicode.cpp UnicodeTables.cpp)`, public include
  directories `${CMAKE_SOURCE_DIR}` and `${CMAKE_SOURCE_DIR}/src/components/Unicode`
  (so both `#include "Unicode.h"` and
  `#include "src/components/Unicode/Unicode.h"` work, as for the other
  components), `target_compile_features(Unicode PUBLIC cxx_std_17)`. No
  other library linked.
- Root `CMakeLists.txt`: `add_subdirectory(src/components/Unicode)` in the
  components list, before `src/components/BuiltinCommands`. Nothing links the
  library yet (builtins--wc makes `BuiltinCommands` link it).
- `src/components/Unicode/CLAUDE.md` (new): what the component is (UTF-8
  decoding, `IsPrintable`, `IsSpace`, `IsNoBreakSpace`, `DisplayWidth`), what
  it stands in for (glibc in a UTF-8 locale) and where it is knowingly
  approximate (unassigned code points printable; rarer scripts' combining
  marks width 1; widths follow the tables above, not every Unicode version's
  East Asian Width data), who uses it (first `wc`, from builtins--wc), and the rule that a builtin
  needing character classes or widths uses this component rather than
  `<cwctype>`/`wcwidth` (locale-dependent and absent on Windows).

### Root `CLAUDE.md` rules that bite

- No interface, no class: the "private constructors and `Create()`" rule
  does not apply (free functions only).
- Rule 7: repo-relative paths only, in comments and docs.
- Build only on Linux to verify; release build.

## Tests

### `tests/unit/components/Unicode.unittests/` (new)

`CMakeLists.txt`: `add_executable(Unicode.unittests UnicodeTest.cpp)`,
`target_link_libraries(Unicode.unittests PRIVATE gtest_main Unicode)`,
`target_compile_features(... cxx_std_17)` (as
`tests/unit/components/Environment.unittests/CMakeLists.txt`); register it in
`tests/unit/CMakeLists.txt` with `add_subdirectory(components/Unicode.unittests)`.
`UnicodeTest.cpp`, suite `UnicodeTest`:

- `DecodesAsciiAndEachSequenceLength`: `"A"` -> Ok U+0041 length 1;
  `"\xC3\xA9"` -> U+00E9, 2; `"\xE4\xB8\xAD"` -> U+4E2D, 3;
  `"\xF0\x9F\x98\x80"` -> U+1F600, 4; `"\xF4\x8F\xBF\xBF"` -> U+10FFFF, 4;
  a NUL byte (size 1) -> Ok U+0000, 1; decoding stops after one character
  (`"ab"` -> length 1).
- `RejectsInvalidBytesOneAtATime`: each of `\x80`, `\xBF`, `\xC0\x80`,
  `\xC1\xBF`, `\xE0\x80\x80` (overlong), `\xED\xA0\x80` (surrogate),
  `\xF4\x90\x80\x80` (above U+10FFFF), `\xF5\x80\x80\x80`, `\xFF`,
  `\xE4\x41` (cut short by `A`) -> Invalid, length 1.
- `ReportsATruncatedSequenceAsIncomplete`: `\xE2\x82` (size 2), `\xF0`
  (size 1), `\xF0\x9F\x98` -> Incomplete, length 0; but `\xE0\x80` (size 2)
  -> Invalid (already not a valid prefix).
- `PrintableClasses`: true for U+0020, U+0041, U+00A0, U+00E9, U+0301,
  U+200B, U+E000, U+0378 (unassigned: printable here), U+1F600, U+10FFFD;
  false for U+0000, U+0001, U+001F, U+007F, U+0085, U+009F, U+2028, U+2029,
  U+FDD0, U+FFFE, U+1FFFF, U+10FFFF, U+D800, U+110000.
- `SpaceClasses`: `IsSpace` true for U+0009-U+000D, U+0020, U+1680, U+2000,
  U+2006, U+2008, U+200A, U+205F, U+3000; false for U+00A0, U+2007, U+202F,
  U+2060, U+200B, `A`. `IsNoBreakSpace` true exactly for U+00A0, U+2007,
  U+202F, U+2060.
- `DisplayWidths`: -1 for U+0001, U+007F, U+2028; 0 for U+0301, U+200B,
  U+2060, U+FEFF, U+FE0F, U+E0001, U+1160, U+302A; 1 for `A`, U+00E9, U+00AD,
  U+00A0, U+0600, U+E000, U+2003; 2 for U+4E2D, U+AC00, U+FF21, U+3000,
  U+1F600, U+1FA70, U+231A, U+20000. (Each of these agrees with glibc 2.39's
  `wcwidth` in `C.UTF-8`, checked when this plan was written.)
- `EveryTableIsSortedAndDisjoint`: if the tables are reachable from the test
  (an internal header), each range array is sorted with `first <= last` and
  no overlap; otherwise assert `DisplayWidth` at every range edge listed
  under Changes.

### Commands

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Unicode
bash ./scripts/test_linux.sh L U
```

(`test_linux.sh`'s filter must match the executable name and is passed as
`--gtest_filter=*<filter>*`: `Unicode` selects `Unicode.unittests` and its
suite `UnicodeTest`.)

## Docs

- `src/components/Unicode/CLAUDE.md` (new), as described under Changes.
- Root `CLAUDE.md`, Architecture component table: a row **Unicode** |
  `src/components/Unicode/` | "UTF-8 decoding, character classes and display
  widths -- a compact, locale-free stand-in for glibc's, for builtins (`wc`)";
  and `Unicode/` in the Directory Structure list under `src/components/`.

## Acceptance

- [ ] `src/components/Unicode/` holds `Unicode.h` (the API exactly as above), `Unicode.cpp`, `UnicodeTables.cpp`, `CMakeLists.txt` (library `Unicode`, no dependencies) and `CLAUDE.md`.
- [ ] The root `CMakeLists.txt` adds the component; nothing else links it yet.
- [ ] `Unicode.unittests` exists, is registered in `tests/unit/CMakeLists.txt`, and every test above passes.
- [ ] The tables are sorted range arrays searched by binary search; every range listed under Changes is in them, and nothing else.
- [ ] No `<cwctype>`, `wcwidth`, `setlocale` or `mbrtowc` anywhere in the component.
- [ ] The root `CLAUDE.md` has the Unicode row and the Directory Structure entry.
- [ ] Linux build passes; all unit tests pass.

## Out of scope

- The `wc` builtin and linking `BuiltinCommands` to this library (builtins--wc).
- Using it anywhere else (ls column widths, `cat -v`, hsh) -- later work.
- Exact glibc tables (assigned/unassigned code points, every combining mark,
  a full East Asian Width table); encodings other than UTF-8; grapheme
  clusters, normalization, case mapping.
