# Quoting and escaping: the haisosfile, ls, WSL to cmd.exe, UNC paths

**The haisosfile** (`src/haisos/HaisosFileParser.cpp`, since commit 8515a90):
a token that starts with `'` or `"` runs to the next quote of the same kind,
taken without the quotes -- and that is all. Unlike a shell:

- nothing is escaped. `\` is an ordinary character, so `"C:\Program Files\x"`
  and `\\server\share` are taken as written, and a quoted token cannot hold its
  own quote: use the other kind (a name holding both cannot be written).
- A quote elsewhere in a token is part of it (`it's`), and a closing quote must
  end its token.
- `${name}` is substituted everywhere but in `CREATE`/`APPEND` content, quoted
  or not, and before the line is split. A value's spaces split it unless the
  reference is quoted (`"${dir}"`), and quotes inside a value are read as
  quoting, where a shell would take them literally. A literal `${name}` cannot
  be written.
- A `#` after whitespace starts a comment unless it is inside quotes. There is
  no line continuation.

Any escape added later must not be `\`, which Windows paths are full of.

**`ls`** (`src/components/BuiltinCommands/commands/Ls.cpp`) prints names as GNU
ls does to a terminal, in its shell-escape style: a name holding a space or
anything a shell reads specially (`$`, `'`, `\`, `*`, ...) is shown quoted, as
`'my file.txt'`. `-N`/`--literal` prints names as they are; `-b`, `-Q`,
`--quoting-style`, `-q` and `--show-control-chars` are parsed but not treated.
An agent reading `ls` output must unquote a name before passing it to an
`os_*` tool, which takes names as they are -- or list with `-N`, or use
`os_list_directory`. (On Windows a name cannot hold `\` or `"`, but can hold
`'`, `$` and spaces.)

**WSL bash to cmd.exe**: a command passed from bash through
`cmd.exe /c "..."` is parsed three times -- by bash, by WSL building a Windows
command line, and by cmd.exe with its own rules (`"`, `^`, `%VAR%`) -- and
quoted paths easily break ("The filename, directory name, or volume label
syntax is incorrect."). Writing the commands to a `.bat` file and running
`cmd.exe /c <its Windows path>` avoids it. `std::system` goes through cmd.exe
too: the Windows test that makes a junction with `mklink /J` quotes both paths.

**UNC paths**:

- cmd.exe cannot have a UNC current directory. Started from a WSL directory --
  `\\wsl.localhost\<distro>\...` to Windows -- it warns "UNC paths are not
  supported" and uses `C:\Windows`, so a Windows script first moves to where it
  belongs, as `scripts/build_windows_on_windows.bat` does with `cd /d`.
  `haisos.exe` itself runs fine with a UNC current directory.
- In the haisosfile a UNC path is written as it is, `\\server\share\dir` or
  `//server/share/dir`, quoted if it holds a space. It is a physical path
  (`src/components/Filesystem/PhysicalPath.h`), but not part of the full
  physical filesystem, which holds only drives
  (`notes/note-physical-directory-jail.md`).
- Wherever `\` is an escape it must be doubled: in bash (unquoted, `\\server`
  becomes `\server`; quote it with `'...'`), in C++ string literals and in
  JSON (`"\\\\server\\share"`). The `//server/share` form needs no escaping
  anywhere. Agents never see host paths, so no UNC path reaches a tool call.
