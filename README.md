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
- [x] Phase 6: pipes
- [ ] Phase 7: background jobs (`&`, `jobs`)
- [ ] Phase 8: signals (Ctrl+C, Ctrl+Z, SIGCHLD)
- [ ] Phase 9: history (persistent file, polish)
- [ ] Phase 10: Linux-style prompt and UI
- [ ] Phase 11: testing, docs and polish

## Project layout

| File | Responsibility |
|---|---|
| `main.cpp` | The shell loop: read a line, tokenize, parse, execute |
| `lexer.h` / `lexer.cpp` | `tokenize()`: quotes, escapes, `$VAR`, `~`, operators (`< > >> 2> \|`), syntax errors. A pure function. |
| `parser.h` / `parser.cpp` | `parse_pipeline()`: tokens become a `Pipeline` of `Command`s (argv + redirections each) |
| `executor.h` / `executor.cpp` | `execute()`: runs built-ins in-process, forks one child per stage, wires `pipe()`s, `waitpid()` |
| `redirect.h` / `redirect.cpp` | `open`/`dup2`/`close` redirection: `apply_redirections()` for children, `RedirectScope` for built-ins |
| `builtins.h` / `builtins.cpp` | `ShellState`, built-in dispatch table, the 8 built-in commands |
| `tests/test_lexer.cpp`, `tests/test_parser.cpp` | Table-driven unit tests (`make test`) |
| `Makefile` | Build rules |

## How a line is processed

```
"sort < in.txt | head -2 > out.txt"
        │
        ▼
  tokenize()        ──►  sort OP(<) in.txt OP(|) head -2 OP(>) out.txt      (lexer.cpp)
        │
        ▼
  parse_pipeline()  ──►  command 1: argv {sort}      redirections {0 < in.txt}
        │                 command 2: argv {head, -2}  redirections {1 > out.txt}   (parser.cpp)
        │
        ▼
  execute()                                                                  (executor.cpp)
        │
        ├─ ONE built-in, no pipe ──► RedirectScope: save fds, redirect, run
        │                             in the shell process, restore fds
        │
        └─ anything else ──► for each command: pipe() + fork()
                              child:  dup2 pipe ends onto 0/1, close the rest,
                                      apply its redirections, execvp (or run the built-in)
                              parent: close its pipe ends, then waitpid() every child
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

## Pipes (Phase 6)

`pipe(fd)` creates a kernel buffer (about 64 KB on Linux) with two ends: `fd[0]` is the
**read end** and `fd[1]` is the **write end**. For `a | b | c` the shell makes a pipe per `|`,
forks one child per command, and uses `dup2` to wire stdout of each stage to the write end and
stdin of the next stage to the read end:

```
a ──fd 1──► [pipe 1] ──fd 0──► b ──fd 1──► [pipe 2] ──fd 0──► c
```

All stages run **at the same time**. The pipe is the flow control: a writer blocks when the buffer
is full and a reader blocks when it is empty.

**The classic bug: unclosed pipe ends.** A reader sees end-of-file only when *every* copy of the
write end is closed. If the parent (or any other child) keeps a copy, the reader waits forever.
So every process closes every pipe descriptor it does not use:

| Process | Closes |
|---|---|
| Child | the original pipe fds, right after `dup2` copied the needed ones onto 0 and 1 |
| Parent | its copies of both ends, right after each `fork()` |

Pipes are created one at a time, so the parent holds at most two pipe ends, and `pipe2(O_CLOEXEC)`
closes any stragglers on `exec` as a safety net.

Rules and details:

- **Status:** a pipeline's exit status is the **last** command's (bash default): `false | true` is 0.
- **Early-exiting reader:** `yes | head -1` finishes because the next `write()` by `yes` gets **SIGPIPE**.
- **Stderr is not piped:** `ls /nonexistent | wc` prints the error on the terminal.
- **Built-ins in a pipeline run in a child**, as in bash. `echo hi | cat` forks for `echo`, and
  `cd /tmp | cat` does **not** change the shell's directory. A single built-in with no pipe still
  runs in the shell process.
- **Redirections win over pipes:** `echo hi > f | cat` writes to `f` and leaves `cat` an empty input;
  `echo x | cat < f` reads from `f`.
- Every stage must be a command: `| cat`, `echo |`, `a || b` and `> f | cat` are syntax errors (status 2).
- The parent waits for every child, so there are no zombies, and flushes its output before forking,
  so children never print buffered text a second time.

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
nexus> ls /bin | head -3
2to3-3.11
X11
Xvfb
nexus> printf "b\na\nc\na\n" | sort | uniq -c | sort -rn
      2 a
      1 c
      1 b
nexus> seq 1 100 | grep 5 | wc -l
19
nexus> cat < notes.txt | sort | head -1 > first.txt
nexus> false | true
nexus> echo $?
0
nexus> cd /tmp | cat
nexus> pwd
/home/akshat/projects
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

**Phase 5: redirection** (automated cases in `make test`, plus manual checks)
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

**Phase 6: pipes** (100 lexer and 58 parser cases in `make test`, plus manual checks)
- [x] Two, three, four, five and 30-stage pipelines produce the right output
- [x] Built-in into external (`echo hi | cat`), external into external, and external into built-in all work
- [x] Large data (`seq 1 200000 | wc -l`) flows through, proving the stages run concurrently
- [x] The reader sees EOF when the writer finishes: no hang (the parent closes its write end)
- [x] `yes | head -3` and `yes | head -100000 | wc -l` terminate (SIGPIPE stops the writer)
- [x] Pipeline status is the last command's (`false | true` is 0, `true | false` is 1, `x | nosuchcmd` is 127)
- [x] Built-ins inside a pipeline run in a child: `cd /usr | cat` and `export V=1 | cat` leave the shell unchanged; `exit 5 | cat` does not exit the shell
- [x] The same built-ins without a pipe still change the shell
- [x] Redirections combine with pipes (`cat < in | sort > out`); an explicit redirect wins over the pipe
- [x] Stderr is not piped; `2>` on one stage captures only that stage's errors
- [x] A failing stage (`nosuchcmd | cat`) does not block the others
- [x] `| cat`, `echo |`, `a || b`, `ls | | wc` and `> f | cat` are syntax errors with status 2
- [x] Quoted or escaped `|` is ordinary text; `echo hi|cat` works without spaces
- [x] Children hold only fds 0, 1, 2 (no stray pipe ends); the shell holds the same 3 fds after 600 pipelines
- [x] The shell's own stdin and stdout are unchanged afterwards
- [x] No duplicated output when the shell's stdout is a file (buffers flushed before fork)
- [x] No zombie processes after 200 pipelines
- [x] Removing the parent's `close(write_end)` makes `echo hi | cat` hang (checked by mutation)

## Known limitations (to be addressed in later phases)

- `&` (background) is not recognized yet: it is an ordinary character (Phase 7)
- No `||`, `&&`, `;`, `|&`, subshells `( )` or command substitution `$( )`
- A pipeline stage with only redirections (`> f | cat`) is rejected
- No `2>&1`, `1>`, `2>>`, `&>` or here-documents (`<<`); `2>&1` is rejected with an error
- A redirection target that starts with `&` is rejected, so a file named `&x` needs `./&x`
- No word splitting of expanded values: `X="a b"; echo $X` stays one argument (zsh-style)
- No `~user`, globbing (`*.cpp`), `$1`-style parameters or `#` comments
- `history` is in-memory only; no persistence yet (Phase 9)
- Ctrl+C also kills the shell, and a foreground pipeline is not in its own process group yet (Phase 8)
