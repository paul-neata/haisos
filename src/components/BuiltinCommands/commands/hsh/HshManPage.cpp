#include "commands/hsh/HshManPage.h"

// The manual page, one raw string literal per section, concatenated: MSVC
// refuses a single string literal past ~16 KB (error C2026), and the page as
// one string would also be unreadable. Every section ends in a blank line
// (the last in a newline alone).

namespace Haisos::Hsh {

std::string HshManPage() {
    return
        R"MAN(NAME
       hsh - the Haisos shell, a command interpreter after dash

)MAN"
        R"MAN(SYNOPSIS
       hsh [-aCefnuvxIimVEbp] [+aCefnuvxIimVEbp] [-o option_name]
           [+o option_name] [command_file [argument ...]]
       hsh -c [-aCefnuvxIimVEbp] [+aCefnuvxIimVEbp] [-o option_name]
           [+o option_name] command_string [command_name [argument ...]]
       hsh -s [-aCefnuvxIimVEbp] [+aCefnuvxIimVEbp] [-o option_name]
           [+o option_name] [argument ...]

)MAN"
        R"MAN(DESCRIPTION
       hsh is a command interpreter after dash (POSIX sh), run as a
       Haisos builtin. It reads commands from a command_file, from the
       command_string of -c, or from its standard input. A command that
       is not one of hsh's own builtins is looked up in PATH and started
       as a Haisos process -- a builtin, a .md agent or a .lua script --
       with the shell's exported variables, working directory and
       standard streams.

       Without -c and without a command_file, and with both its standard
       input and its standard error terminals, hsh is interactive: it
       prompts with PS1 ("$ ") before each command and with PS2 ("> ")
       while a command is incomplete, and an error does not end it. -i
       makes it interactive whatever the input. RUN -i /bin/hsh in a
       haisosfile gives an interactive shell on the console; with the
       builtins in /bin, ENV PATH=/bin lets it find commands by name.

)MAN"
        R"MAN(OPTIONS
       +X turns option X off; -o NAME turns a named option on (+o: off).

       -a, -o allexport   export every variable assigned
       -c                 read commands from the command_string operand
       -C, -o noclobber   > does not overwrite an existing file
       -e, -o errexit     exit when an untested command fails
       -f, -o noglob      no pathname expansion
       -i, -o interactive prompt for commands
       -n, -o noexec      read commands without running them
       -s, -o stdin       read commands from standard input
       -u, -o nounset     expanding an unset variable is an error
       -x, -o xtrace      show each command before running it

       The other dash options (-b -E -I -l -m -p -v -V and the names
       ignoreeof, monitor, verbose, vi, emacs, notify, privileged, nolog
       and debug) are accepted, reported as not treated and not acted on.

)MAN"
        R"MAN(QUOTING AND ESCAPING
       A backslash quotes the next character; a backslash-newline is a
       line continuation, removed everywhere but in single quotes. Single
       quotes keep everything until the next single quote. Inside
       "double quotes" $, `, \ and a closing " stay special.

       Example: echo 'single $HOME' "double $HOME" \$HOME

)MAN"
        R"MAN(PARAMETERS AND EXPANSIONS
       $name and ${name} expand a variable; $1 ... are the positional
       parameters, $# their count, $@ and $* all of them, $? the last
       exit status, $$ the shell's pid, $! the last background pid, $-
       the options that are on and $0 the shell's name. ${x:-w}, ${x-w},
       ${x:=w}, ${x=w}, ${x:?w}, ${x?w}, ${x:+w}, ${x+w}, ${#x}, ${x#p},
       ${x##p}, ${x%p} and ${x%%p} are the ${...} forms. $(...) and `...`
       substitute a command's output, $((...)) an arithmetic result. The
       words are then split on IFS, globbed (*, ?, [...]) and stripped of
       their quotes; a leading ~ is HOME. Expansions run in that order.

       Example: x=file.txt; echo ${x%.txt} $((1 + 2)) $(echo sub) *.md

)MAN"
        R"MAN(PIPELINES
       a | b | c runs its stages at the same time, connected by pipes; a
       leading ! negates the status. The pipeline's status is the last
       command's.

       Example: ls /bin | wc -l

)MAN"
        R"MAN(REDIRECTIONS
       <f      standard input read from f
       >f      standard output written to f, truncated
       >|f     the same during -C (noclobber), which otherwise refuses to
               replace an existing file
       >>f     standard output appended to f
       <>f     f open for reading and writing
       n>&m    descriptor n a copy of m
       n<&m    the same for input descriptors
       n>&-    descriptor n closed (n<&- for inputs)
       &>f     standard output and standard error both to f
       <<<w    standard input the word w expanded, plus a newline
       A number in front is the IO number. Heredocs are below.

       Example: ls /nope > /out.txt 2>&1

)MAN"
        R"MAN(HERE-DOCUMENTS (HEREDOCS)
       <<WORD reads the following lines, up to one that is WORD, as the
       command's standard input; <<-WORD strips leading tabs. A quoted
       delimiter turns off the $, ` and $((...)) expansions in the body.

       Example: cat <<EOF
                  a line
                EOF

)MAN"
        R"MAN(LISTS
       Commands join with ; or a newline (in sequence), && (the second
       runs when the first succeeds), || (when it fails) and & (in the
       background: programs run there directly; a builtin, function or
       compound command runs in a child hsh, which sees only the exported
       variables). $! is the last background job's pid; wait waits for it.

       Example: ls /nope || echo "failed: $?"

)MAN"
        R"MAN(GROUPS AND SUBSHELLS
       { list; } runs the list in the shell itself, ( list ) in a
       subshell: variables, the working directory, options, functions and
       descriptors a subshell changes do not leak out of it.

       Example: (cd /bin; ls) | wc -l

)MAN"
        R"MAN(IF
       if list; then list; [elif list; then list;] ... [else list;] fi
       runs the body of the first condition that succeeds (status 0).
       test and [ are the usual condition builtins.

       Example: if [ -d /bin ]; then echo yes; else echo no; fi

)MAN"
        R"MAN(WHILE AND UNTIL
       while list; do list; done repeats while its condition succeeds,
       until list; do list; done while it fails. break [n] leaves n
       loops, continue [n] starts their next iteration; read fills
       variables from lines of the input.

       Example: while read line; do echo "<$line>"; done < /notes.txt

)MAN"
        R"MAN(FOR
       for name [in word ...] do list; done runs the list once per word,
       its variable set to each; without in it runs over the positional
       parameters ("$@").

       Example: for f in a b c; do echo $f; done

)MAN"
        R"MAN(CASE
       case word in pattern [| pattern ...]) list ;; ... esac runs the
       list of the first matching pattern; * ? [...] match as in file
       names, | separates alternatives, ;; ends an item.

       Example: case "$1" in hi*) echo greeted;; *) echo plain;; esac

)MAN"
        R"MAN(FUNCTIONS
       name() command defines a function; called as a command it runs its
       body with its own positional parameters ($1 ..., $0 unchanged) and
       return [n] ends it. Redirections after the body apply on every
       call. Lookup order: special builtin, function, regular builtin,
       PATH.

       Example: greet() { echo "hello $1"; }; greet world

)MAN"
        R"MAN(BUILTIN COMMANDS
       . file        run a file's commands in this shell (special)
       :             do nothing, status 0 (special)
       [ expr ]      test's other name
       break [n]     leave n loops (special)
       cd [dir]      change the working directory
       continue [n]  next iteration of n loops (special)
       eval args     the arguments run as a command (special)
       exec [args]   replace the shell, or keep its redirections (special)
       exit [n]      end the shell (special)
       export names  mark variables for the environment (special)
       false         status 1
       read names    a line of input, split on IFS
       readonly nm.  mark variables unchangeable (special)
       return [n]    end the function or dot script running (special)
       set ...       options, positional parameters, a listing (special)
       shift [n]     drop positional parameters (special)
       test expr     a condition's truth
       true          status 0
       unset names   remove variables (special)
       wait [pids]   wait for background jobs

)MAN"
        R"MAN(EXIT STATUS
       0 is success, 1 a false condition; 2 a syntax or usage error; 126
       a command found but not runnable, 127 one not found; 128+n death
       by a signal -- 141 a broken pipe, 143 stopped. The status of a
       pipeline is its last command's, of a list its last command's, of a
       compound command its body's last command's, of a script its last
       command run.

)MAN"
        R"MAN(DIFFERENCES FROM DASH
       &> and <<< are bash's operators; ~user is not expanded; ${x:} is a
       "Bad substitution" expansion error; the messages of a $(...)'s
       syntax error come from parsing its inner text alone; $0 is "hsh"
       unless a script or -c command_name names it; --help and --version
       count only as the first argument; the prompts and PS4 are written
       as they are, not parameter-expanded; -v is accepted, not acted
       on; a background (&) builtin, function or compound command runs in
       a child hsh, which sees only the exported variables; test -r -w -x
       -O -G only test that the file is there, -h and -L are never true;
       there is no job control, line editing, history, trap, local or
       getopts; commands are run as Haisos processes, never by a #! line.

)MAN"
        R"MAN(SEE ALSO
       dash(1): https://man7.org/linux/man-pages/man1/dash.1.html
       man(1), and the other Haisos builtins: cat(1), echo(1), ls(1),
       mkdir(1), pwd(1), wc(1).
)MAN";
}

} // namespace Haisos::Hsh
