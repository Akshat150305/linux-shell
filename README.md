# NexusShell

A Linux shell written from scratch in C++ using raw POSIX system calls
(`fork`, `execvp`, `waitpid`, and later `pipe`, `dup2`, signals).
Built as an Operating Systems learning and portfolio project, one feature at a time.

> **Status:** in development. See the roadmap below.

## Build and run

```bash
g++ -Wall -Wextra -std=c++17 main.cpp -o nexusshell
./nexusshell
```

Requires Linux, Ubuntu or WSL, and g++ 9 or newer.

## Roadmap

- [x] Phase 1: interactive shell loop
- [x] Phase 2: external command execution (`fork` / `execvp` / `waitpid`)
- [ ] Phase 3: built-in commands (`cd`, `pwd`, `echo`, `exit`, `clear`, `help`, `history`, `export`)
- [ ] Phase 4: lexer and parser (quotes, escapes, `$VAR`)
- [ ] Phase 5: I/O redirection (`>`, `>>`, `<`, `2>`)
- [ ] Phase 6: pipes
- [ ] Phase 7: background jobs (`&`, `jobs`)
- [ ] Phase 8: signals (Ctrl+C, Ctrl+Z, SIGCHLD)
- [ ] Phase 9: history (in-memory and persistent)
- [ ] Phase 10: Linux-style prompt and UI
- [ ] Phase 11: testing, docs and polish

## How command execution works (Phase 2)

```
nexus> ls -l
   │
   ├─ fork() ──► child:  execvp("ls", ["ls","-l"])   (becomes `ls`)
   │
   └─ parent:    waitpid(child)                      (blocks until it ends)
                 └─ reads exit status, prompts again
```

The shell forks rather than exec'ing itself, so it survives to run the next command.
Exit statuses follow shell convention:

| Status | Meaning |
|---|---|
| `0` | success |
| `1..255` | command's own exit code |
| `126` | found but not executable |
| `127` | command not found |
| `128 + N` | killed by signal N (for example 139 = SIGSEGV) |

## Testing checklist

**Phase 1: shell loop**
- [x] Blank line re-prompts without crashing
- [x] `exit` terminates the shell
- [x] Ctrl+D (EOF) terminates cleanly

**Phase 2: external commands**
- [x] `echo hello   world` runs with extra whitespace collapsed
- [x] Unknown command prints `command not found` (status 127)
- [x] Non-executable file gives `Permission denied` (status 126)
- [x] `false` returns status 1
- [x] A child killed by SIGSEGV returns 139
- [x] Shell keeps running after a failed command
- [x] Shell blocks until a long command (`sleep 1`) finishes
- [x] No zombie processes after many commands

## Known limitations (to be addressed in later phases)

- No quoting or escaping: `echo "a b"` is split into two arguments (Phase 4)
- No built-ins, so `cd` has no effect (Phase 3)
- Ctrl+C also kills the shell (Phase 8)
