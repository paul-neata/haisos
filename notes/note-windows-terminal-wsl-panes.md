# Opening a WSL bash tab with a Claude split pane from Windows

How to open, from Windows, a Windows Terminal tab running WSL bash with a
split pane running `claude`, both in the same folder given by an environment
variable. Tested live on Windows Terminal 1.24, WSL distro `Ubuntu-24.04`,
with each pane's real working directory read from `/proc/<pid>/cwd`.

From a `.cmd` file or Command Prompt:

```bat
set "HAISOS_DIR=/home/paul/src/haisos1"
wt.exe -w 0 new-tab -p "Ubuntu-24.04" --title bash -d "%HAISOS_DIR%" ; split-pane -V -p "Ubuntu-24.04" --title claude wsl.exe -d Ubuntu-24.04 --cd "%HAISOS_DIR%" -e claude
```

From PowerShell (`;` escaped as `` `; ``):

```powershell
$env:HAISOS_DIR = "/home/paul/src/haisos1"
wt.exe -w 0 new-tab -p "Ubuntu-24.04" --title bash -d "$env:HAISOS_DIR" `; split-pane -V -p "Ubuntu-24.04" --title claude wsl.exe -d Ubuntu-24.04 --cd "$env:HAISOS_DIR" -e claude
```

The folder may be a Linux path, a Windows path (`C:\src\haisos1` becomes
`/mnt/c/src/haisos1`) or `\\wsl.localhost\Ubuntu-24.04\...`. `-w 0` opens
in the current window, `-w new` in a new one; `-V` splits side by side, `-H`
one above the other.

## Gotchas found

- **Profile names are exact.** There was no profile `Ubuntu`: the profiles
  are `Ubuntu 24.04.1 LTS` (the default, from the Ubuntu app) and the hidden,
  auto-generated `Ubuntu-24.04`. An unknown `-p` name silently falls back to
  the default profile.
- **`-d` behaves differently per profile and per pane:**
  - `-p "Ubuntu-24.04"` with no command of its own: Windows Terminal turns
    `-d X` into `wsl.exe --cd "X" -d Ubuntu-24.04`, for Linux, Windows and
    `\\wsl.localhost` paths alike.
  - `-p "Ubuntu 24.04.1 LTS"` runs `ubuntu2404.exe` and drops `-d`.
  - A pane with its own command (`... wsl.exe -e claude`) uses `-d` as the
    Windows working directory of `wsl.exe`, so a Linux path there makes the
    pane fail to start. Such panes take the folder from `wsl.exe --cd`, and no
    `-d`.
  - `-d .` means the current Windows folder (e.g. `C:\Users\Paul`).
- **`;` separates Windows Terminal subcommands everywhere, even inside
  quotes.** Inside a pane's own command (`bash -c "a; b"`), write `\;`.
- **Quoting through `bash -> cmd.exe /c '...'` is unreliable.** Test from a
  real `.cmd` file or PowerShell, as Windows would run it.
- **Variables are expanded by the calling shell** (`%VAR%`, `$env:VAR`)
  before `wt.exe` runs. Panes of an already running Windows Terminal do not
  inherit variables set in the calling shell.
- **`-e claude` skips bash entirely**, so `.bashrc` aliases (such as `ccd`)
  are not there; `claude` is found because it is in `/usr/local/bin`. Use
  `-e bash -ic ccd` for the alias, or `-e bash -c "claude\; exec bash"` to keep
  a shell after Claude exits.

## `~/.bashrc` overrides the folder

Line 129 of `~/.bashrc` was `cd /mnt/c/src`, run by every interactive bash,
so the plain bash pane always ended in `/mnt/c/src`, whatever `--cd` or `-d`
said. (`bash -c` does not read `.bashrc`, which hid this in a first test.)
The fix, verified with a copy of `.bashrc` passed via `--rcfile`: only jump
when the shell starts in the home folder, as a profile without a folder does
(`wsl.exe ~`):

```bash
[ "$PWD" = "$HOME" ] && cd /mnt/c/src
```

Not applied to `~/.bashrc` yet.
