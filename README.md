# NexusShell

A Linux shell written from scratch in C++ using raw POSIX system calls
(`fork`, `execvp`, `waitpid`, `chdir`, `setenv`, and later `pipe`, `dup2`, signals).
Built as an Operating Systems learning and portfolio project, one feature at a time.

> **Status:** in development. See the roadmap below.

## Build and run

```bash
make            # builds ./nexusshell
./nexusshell
make test       # runs the lexer and parser unit tests
make clean      # removes build outputs
```

Requires Linux, Ubuntu or WSL, g++ 9 or newer, and make.

## Roadmap

- [x] Phase 1: interactive shell loop
- [x] Phase 2: external command execution (`fork` / `execvp` / `waitpid`)
- [x] Phase 3: built-in commands (`cd`, `pwd`, `echo`, `exit`, `clear`, `help`, `history`, `export`)
- [x] Phase 4: lexer (quotes, escapes, `$VAR`, `~`)
- [x] Phase 5: I/O redirection (`>`, `>>`, `<`, `2>`)
- [ ] Phase 6: pipes
- [ ] Phase 7: background jobs (`&`, `jobs`)
- [ ] Phase 8: signals (Ctrl+C, Ctrl+Z, SIGCHLD)
- [ ] Phase 9: history (persistent file, polish)
- [ ] Phase 10: Linux-style prompt and UI
- [ ] Phase 11: testing, docs and polish

## Project layout

| File | Responsibility |
|---|---|
| `main.cpp` | Shell loop and external command execution (`fork`/`execvp`/`waitpid`) |
| `lexer.h` / `lexer.cpp` | `tokenize()`: quotes, escapes, `$VAR`, `~`, redirection operators, syntax errors. A pure function. |
| `parser.h` / `parser.cpp` | `parse_command()`: tokens become a `Command` (argv + redirections) |
| `redirect.h` / `redirect.cpp` | `open`/`dup2`/`close` redirection: `apply_redirections()` for children, `RedirectScope` for built-ins |
| `builtins.h` / `builtins.cpp` | `ShellState`, built-in dispatch table, the 8 built-in commands |
| `tests/test_lexer.cpp`, `tests/test_parser.cpp` | Table-driven unit tests (`make test`) |
| `Makefile` | Build rules |

## How a line is processed

```
"sort < in.txt > out.txt"
        │
        ▼
  tokenize()       ──►  sort  OP(<)  in.txt  OP(>)  out.txt      (lexer.cpp)
        │
        ▼
  parse_command()  ──►  argv = {sort}
        │                redirections = {0 < in.txt, 1 > out.txt}  (parser.cpp)
        │
        ├─ built-in?  ──► RedirectScope: save fds, redirect, run
        │                  in the shell process, restore fds      (builtins.cpp, redirect.cpp)
        │
        └─ otherwise  ──► fork() ─► child: open + dup2 + close, then execvp(argv)
                                    parent: waitpid(child)        (main.cpp)
```

The shell forks rather than exec'ing itself, so it survives to run the next command.
Exit statuses follow shell convention:

| Status | Meaning |
|---|---|
| `0` | success |
| `1..255` | command's own exit code |
| `2` | syntax error (for example an unterminated quote) |
| `126` | found but not executable |
| `127` | command not found |
| `128 + N` | killed by signal N (for example 139 = SIGSEGV) |

## Parsing rules (Phase 4)

Programs never see quotes: `echo "a b"` reaches `echo` as two arguments, `echo` and `a b`.
The shell's lexer does that work before `execvp`.

| Syntax | Meaning | Example (input → argument) |
|---|---|---|
| space / tab (unquoted) | separates words | `ls   -l` → `ls`, `-l` |
| `'...'` | everything literal, no escapes, no expansion | `'a $HOME'` → `a $HOME` |
| `"..."` | spaces kept; `$VAR` expands; `\"` `\\` `\$` are escapes | `"hi $USER"` → `hi akshat` |
| `\x` (unquoted) | next character is literal | `a\ b` → `a b` |
| `$VAR`, `${VAR}` | environment variable; unset becomes empty | `$HOME/src` → `/home/akshat/src` |
| `$?`, `$$` | last exit status, shell PID | `echo $?` → `0` |
| `~` at word start | home directory (`~` and `~/path` only) | `~/src` → `/home/akshat/src` |

Details worth knowing:

- Quoted and unquoted pieces join into one word: `ab"cd"'ef'` is the single word `abcdef`.
  This is why `export MSG="hello world"` works.
- `""` is a real empty argument and is kept. An unquoted `$UNSET` expands to nothing and
  the word disappears, so `ls $UNSET` runs plain `ls`.
- A `$` that starts no valid reference (`$`, `$ x`, `$1`) stays a literal `$`.
- Syntax errors (unterminated quote, trailing backslash, malformed `${...}`) print a message,
  skip the command and set the status to 2.

## I/O redirection (Phase 5)

Every process has a table of file descriptors: `0` is stdin, `1` is stdout, `2` is stderr.
Programs just read fd 0 and write fd 1 without knowing where those lead. Redirection works
because the shell rewires the table **before** the program starts, and `exec` keeps open
descriptors:

```
child after fork():
    fd = open("out.txt", O_WRONLY|O_CREAT|O_TRUNC, 0666)    // say fd 3
    dup2(fd, 1)          // fd 1 now points at out.txt
    close(fd)            // drop the spare copy
    execvp("ls", ...)    // ls writes to fd 1, unaware it is a file
```

The parent shell's descriptors are never touched, because the rewiring happens in the child.

| Operator | Descriptor | `open()` flags |
|---|---|---|
| `> file` | 1 (stdout) | `O_WRONLY \| O_CREAT \| O_TRUNC` (empties the file) |
| `>> file` | 1 | `O_WRONLY \| O_CREAT \| O_APPEND` |
| `< file` | 0 (stdin) | `O_RDONLY` |
| `2> file` | 2 (stderr) | same as `>` |

Rules and details:

- Redirections apply **left to right**: `cmd > a > b` creates both files and `b` wins.
- They may appear anywhere in the line (`> out echo hi`) and need no spaces (`echo hi>out`).
- `2>` is an operator only at the **start of a word**: `echo 2 > f` writes `2`, `echo a2>f` writes `a2`,
  and `echo 2>f` redirects stderr.
- Operators are recognized only when **unquoted**: `echo ">"` prints a character.
- If a redirection fails (missing directory, target is a directory), the command is **not run**
  and the status is 1.
- A line with only redirections (`> file`) creates or truncates the file, as in bash.
- **Built-ins have no child to rewire**, so `RedirectScope` saves the shell's descriptor with
  `fcntl(F_DUPFD_CLOEXEC)`, redirects, runs the built-in, flushes output, then restores it with `dup2`.
  Skipping the flush or the restore would send output to the wrong place.
- Files are opened with `O_CLOEXEC`, so a spare descriptor can never leak into a program.

## Why built-ins exist

Every process has its own working directory and environment. A forked child gets a
*copy*, so a child can never change its parent's. Commands that change or read the
**shell's own state** must therefore run inside the shell process, with no fork:

| Built-in | Why it must be built-in | Mechanism |
|---|---|---|
| `cd` | changes the shell's own directory | `chdir()`, then `getcwd()` + `setenv("PWD"/"OLDPWD")` |
| `export` | changes the shell's own environment, which children inherit via `execvp` | `setenv()` |
| `exit` | the shell itself must stop | sets `ShellState::running = false` |
| `history` | reads the shell's memory | in-memory `vector<string>` |
| `pwd`, `echo`, `help`, `clear` | could be external, built in for speed and portability | `getcwd()`, `cout`, ANSI escape `ESC[H ESC[2J` |

## Usage examples

```text
nexus> echo "hello   world" 'single $HOME' $HOME
hello   world single $HOME /home/akshat
nexus> export GREETING="hello world"
nexus> /bin/echo "$GREETING!"
hello world!
nexus> cd ~/projects
nexus> pwd
/home/akshat/projects
nexus> cd -
/home/akshat
nexus> echo "oops
nexus: syntax error: unterminated double quote
nexus> echo $?
2
nexus> ls /tmp > listing.txt
nexus> ls /nonexistent 2> errors.txt
nexus> cat errors.txt
ls: cannot access '/nonexistent': No such file or directory
nexus> echo "line one" > notes.txt
nexus> echo "line two" >> notes.txt
nexus> sort < notes.txt > sorted.txt
nexus> cat sorted.txt
line one
line two
nexus> echo ">" is just text
> is just text
nexus> history
    1  echo "hello   world" 'single $HOME' $HOME
    ...
nexus> exit 3
```

## Testing checklist

**Phase 1: shell loop**
- [x] Blank line re-prompts without crashing
- [x] `exit` terminates the shell
- [x] Ctrl+D (EOF) terminates cleanly

**Phase 2: external commands**
- [x] Unknown command prints `command not found` (status 127)
- [x] Non-executable file gives `Permission denied` (status 126)
- [x] `false` returns status 1
- [x] A child killed by SIGSEGV returns 139
- [x] Shell keeps running after a failed command
- [x] Shell blocks until a long command (`sleep 1`) finishes
- [x] No zombie processes after many commands

**Phase 3: built-ins**
- [x] `pwd` prints the current directory
- [x] `cd /tmp` changes directory for later commands; `pwd` confirms
- [x] `cd` with no argument goes to `$HOME`; `cd -` goes back and prints the path
- [x] `cd /nonexistent`, `cd file`, `cd a b` print an error and return status 1
- [x] `cd` with `HOME` or `OLDPWD` unset prints an error instead of crashing
- [x] `PWD` and `OLDPWD` are updated and visible to child programs (`env`)
- [x] `echo a   b` prints `a b`; `echo -n` omits the newline
- [x] `exit`, `exit 3`, `exit 256` (becomes 0), `exit -1` (becomes 255)
- [x] `exit abc` prints an error and exits with 2; `exit 1 2` errors and the shell keeps running
- [x] `export FOO=bar` is visible to child programs; values containing `=` and empty values work
- [x] `export 1BAD=x` prints `not a valid identifier` and returns 1
- [x] `export` with no arguments lists the environment, sorted
- [x] `history` lists numbered commands, skips blank lines, includes itself
- [x] `clear` emits the ANSI clear-screen sequence
- [x] `help` lists the built-ins
- [x] External commands still work after built-ins; Ctrl+D keeps the last status

**Phase 4: parsing** (automated lexer cases in `make test`, plus manual checks)
- [x] Extra whitespace and tabs between words are collapsed
- [x] `'...'` keeps everything literal, including `$VAR` and backslashes
- [x] `"..."` keeps spaces, expands `$VAR`, and honours `\"` `\\` `\$`
- [x] Backslash escapes a space, quote, `$` or backslash outside quotes
- [x] Adjacent quoted and unquoted pieces concatenate into one word
- [x] `""` is kept as an empty argument; an unquoted unset `$VAR` disappears
- [x] `$VAR`, `${VAR}`, `$?` and `$$` expand; a lone `$` stays literal
- [x] `~` and `~/path` expand only when unquoted at the start of a word
- [x] Quoted command names work (`"ls" -d /tmp`), as do command names from variables
- [x] An external program receives the exact expected `argv` (verified with a small C program)
- [x] Unterminated quote, trailing backslash and bad `${...}` give a syntax error, status 2, and the shell keeps running
- [x] `history` records the raw line, including lines with syntax errors
- [x] Very long lines with thousands of arguments work (no fixed-size buffers)
- [x] The unit tests fail when the lexer is deliberately broken (checked by mutation)

**Phase 5: redirection** (87 lexer and 36 parser cases in `make test`, plus manual checks)
- [x] `>` creates a file with mode 0644 (under umask 022) and truncates an existing one
- [x] `>>` appends; `<` feeds a file to stdin; `<` and `>` work together (`sort < in > out`)
- [x] `2>` captures stderr only; `> o 2> e` splits stdout and stderr, in either order
- [x] Operators work without spaces (`echo a>f`, `cat<in>out`) and before the command (`> f echo hi`)
- [x] `cmd > a > b` creates both files and the last one wins
- [x] `echo 2 > f` and `echo a2>f` write text; `echo 2>f` redirects stderr
- [x] Quoted or escaped `">"`, `"<"`, `">>"`, `"2>"` are ordinary text
- [x] Built-ins can be redirected (`echo`, `pwd`, `help`, `history`, and `2>` on a failing `cd`)
- [x] The shell's own stdin, stdout and stderr are unchanged afterwards, even after failed redirections
- [x] `help > file` works when the shell's stdout is itself a file (buffered output reaches the right place)
- [x] `> file` with no command creates or truncates it, status 0
- [x] Unwritable or missing targets print an error, skip the command and set status 1
- [x] An operator with no file name is a syntax error (status 2); `2>&1` gives a clear "not supported yet" error and no file named `&1`
- [x] Failed `execvp` messages go to the redirected stderr, as in bash
- [x] No leaked file descriptors in the shell (500 redirected commands) or in child programs (`ls /proc/self/fd` shows only 0, 1, 2)
- [x] No zombie processes after many redirected commands

## Known limitations (to be addressed in later phases)

- No `2>&1`, `1>`, `2>>`, `&>` or here-documents (`<<`); `2>&1` is rejected with an error
- A redirection target that starts with `&` is rejected, so a file named `&x` needs `./&x`

- No word splitting of expanded values: `X="a b"; echo $X` stays one argument (zsh-style)
- No `~user`, globbing (`*.cpp`), `$1`-style parameters, command substitution or `#` comments
- `history` is in-memory only; no persistence yet (Phase 9)
- Pipes and `&` are not recognized yet: `|` and `&` are ordinary characters (Phases 6-7)
- Ctrl+C also kills the shell (Phase 8)
