# NexusShell: interview notes

Short answers to the questions this project is most likely to raise, each tied to the code that demonstrates it.
Say the answer first, then point at the file. Section names in the README ("Design notes") have the long versions.

## Processes

**What happens when you type `ls -l`?**
The shell reads the line, tokenizes and parses it, then `fork()`s. The child calls `execvp("ls", ...)`, which searches
`$PATH` and replaces the child's memory image with the new program. The parent waits (`executor.cpp`, `JobTable::run_in_foreground`).
If `execvp` returns, it failed: the child prints the error and calls `_exit(127)` (not found) or `_exit(126)` (not executable).

**Why fork and then exec, instead of one call?**
The gap between the two is where the shell sets the child up: `setpgid`, `dup2` for pipes and redirections, closing
descriptors, resetting signals. The new program inherits all of it. It also keeps the shell alive: `exec` alone would replace the shell.

**Why `_exit` and not `exit` in the child?**
`exit` runs `atexit` handlers and flushes the stdio buffers the child inherited from the parent, which would print the
parent's buffered output a second time. That is also why the shell calls `flush_output()` before every `fork()`.

**Zombie versus orphan?**
A zombie is a child that has exited but whose parent has not collected its status with `wait`; it holds only a process-table
entry. An orphan is a child whose parent died; `init` adopts it. Here the `SIGCHLD` handler reaps every child, so the
shell leaves no zombies (the pseudo-terminal tests start 25 background jobs and count zero).

**Why are `cd` and `export` built-ins?**
A process's working directory and environment belong to that process, and a child gets a copy. A `cd` run in a child
would change only the child. Built-ins run inside the shell process (`builtins.cpp`). The corollary, which the examples
show: `cd /tmp | cat` does **not** change the shell's directory, because a built-in in a pipeline runs in a forked child.

**What does `$?` hold when a program is killed by a signal?**
`128 + N`, by convention: Ctrl+C is `130` (128 + `SIGINT` 2), `SIGSEGV` is `139`, a job stopped by Ctrl+Z is `148`.
The raw `wait()` status is decoded with `WIFEXITED` / `WEXITSTATUS` / `WIFSIGNALED` / `WTERMSIG` (`jobs.cpp`).

## File descriptors, redirection and pipes

**How does `cmd > file` work?**
In the child, before `exec`: `open(file, O_WRONLY|O_CREAT|O_TRUNC)` returns a new descriptor, `dup2(fd, 1)` makes
descriptor 1 point at the file, then the spare is closed. The program writes to "stdout" without knowing (`redirect.cpp`).

**How do you redirect a built-in, which runs in the shell itself?**
`RedirectScope` is an RAII object: it saves a copy of the descriptor with `fcntl(F_DUPFD_CLOEXEC, 10)`, applies the
redirection, and its destructor restores the original, so the shell's own stdout comes back.

**How does `a | b` work? Why did a missing `close()` hang `echo hi | cat`?**
`pipe2()` gives a read end and a write end. The first child `dup2`s the write end onto stdout, the second the read end onto
stdin. A reader sees end-of-file only when **every** copy of the write end is closed, including the parent's. If the
parent keeps its copy open, `cat` waits forever. This was verified by removing the `close` and watching the test hang (`executor.cpp`).

**What is `O_CLOEXEC` for?**
It closes a descriptor automatically on `exec`, so stray descriptors (pipe ends, the saved copies) never leak into the
programs the shell runs. The tests check that a background job sees only descriptors 0, 1 and 2.

**What is the exit status of a pipeline?**
The last stage's (`false | true` gives 0), as in POSIX shells without `pipefail`.

## Signals and job control

**Who receives Ctrl+C?**
The terminal driver turns it into `SIGINT` and sends it to the **foreground process group** of the terminal, nobody else.
So each job is its own process group (`setpgid`), and the shell hands the terminal to the right group with `tcsetpgrp`
(`signals.cpp`, `executor.cpp`). The shell ignores `SIGTSTP`, `SIGQUIT`, `SIGTTIN` and `SIGTTOU` and only notes `SIGINT`.

**Why do both parent and child call `setpgid`?**
Either may run first after `fork()`. If only the parent did it, the child could `exec` and read the terminal before it
was in its group; if only the child did it, the parent could act on the job too early. Calling it on both sides removes the race.

**How does Ctrl+Z / `fg` / `bg` work?**
Ctrl+Z makes the group stop. `waitpid(..., WUNTRACED)` reports "stopped" (plain `waitpid` only reports death), the shell
saves the terminal settings, takes the terminal back and prints `Stopped`. `bg` sends `SIGCONT` to the group
(`kill(-pgid, SIGCONT)`); `fg` restores the job's saved terminal settings, hands it the terminal, sends `SIGCONT` and waits.

**What may a signal handler safely do?**
Only async-signal-safe calls. The `SIGCHLD` handler does just `waitpid()`, writes `(pid, status)` into a fixed array and
saves/restores `errno`. All the real work (updating the job table, printing) happens later in ordinary code,
`JobTable::refresh()`. Two reapers would steal each other's statuses, so the handler is the only place that calls `waitpid(-1, ...)`.

**What is a lost wakeup, and how is it avoided?**
Checking "is the job done?" and then sleeping leaves a gap: if `SIGCHLD` arrives inside it, the shell sleeps forever. The
fix is to block the signal, check, and use `sigsuspend()`, which unblocks and sleeps in one atomic step
(`JobTable::run_in_foreground`). The line editor does the same for Ctrl+C with `ppoll()` and a saved signal mask.

**Why does the shell block `SIGCHLD` around `fork()`?**
Otherwise a fast child could exit, and its status be processed, before the job table knew the child existed.

**Why reset signals in the child?**
A handler is reset by `exec`, but an *ignored* signal stays ignored through `exec`, and a forked built-in never execs.
`reset_child_signals()` restores the defaults and clears the signal mask.

## Terminal and line editing

**How do arrow keys work without readline?**
In canonical mode the kernel delivers a whole line at Enter. The editor clears `ICANON` and `ECHO` with `termios`,
reads byte by byte, echoes characters itself, and decodes escape sequences (`ESC [ A` is Up). `ISIG` stays on so Ctrl+C
still works. The old settings come back (RAII) before any command runs, so children never see raw mode (`input.cpp`).

## Design trade-offs

- **Built-in versus external:** `pwd`, `echo`, `help`, `clear` could be external programs; they are built in for speed and
  portability. `cd`, `export`, `exit`, `jobs`, `fg`, `bg` and `history` must be built in because they touch shell state.
- **Events array of 1024:** the handler cannot allocate memory, so it uses a fixed array; if it ever fills,
  `take_child_events()` collects the rest synchronously with `SIGCHLD` blocked.
- **Pure functions for parsing, history and prompt formatting:** text in, data out, so they are tested by hundreds of
  table-driven cases (308 unit cases in all) without a terminal. Anything that needs a terminal is tested on a pseudo-terminal instead.
- **Script mode versus interactive mode:** when stdin is not a terminal there is no job control, banner, colours or history
  file, and the prompt is a fixed `nexus> `. That keeps piped output deterministic and keeps tests from touching a real history.

## Bugs found while building it (and what each taught)

1. **Missing `close()` hung a pipeline.** The parent held a copy of the write end, so the reader never saw EOF. Lesson: in a
   pipeline, every process closes every pipe descriptor it does not use.
2. **`jobs | cat` would have misjudged the jobs (caught before shipping).** The forked child inherits a copy of the job
   table but the jobs are not its children, so it would have concluded they were gone. Fix: the table remembers its owner PID and does nothing in any other process.
3. **A possible lost Ctrl+C at the prompt (found by reasoning, not by a failing test).** A flag check followed by `read()` leaves a window. Fix: block `SIGINT`, check, then `ppoll()`
   with the old mask.
4. **`yes | head -1` printed "Broken pipe" under CI.** The CI runner starts programs with `SIGPIPE` ignored, and ignored signals
   survive `exec`. It passed on a local terminal and failed on the first CI run. Fix: `reset_child_signals()` also restores
   `SIGPIPE`, with a smoke test that starts the shell with it ignored.
5. **A test matched a repainted prompt instead of a fresh one.** The line editor redraws `\r<prompt><text>` while you edit.
   Fix: the test harness counts a prompt only at the start of a line.

## Questions to ask yourself before the interview

- Can I draw `a | b | c` with its descriptors, forks and closes, and say who closes what?
- Can I explain why Ctrl+C does not kill the shell, and what `tcsetpgrp` changes?
- Which of my tests would fail if I deleted `setpgid`, and how do I know? (Nine of the pseudo-terminal tests, checked by mutation.)
- What would I add next, and what would it cost? (`&&` and `;` need a command-list node in the parser; `$( )` needs
  a pipe plus a recursive evaluation; Tab completion needs a word-under-cursor lookup in the line editor.)
