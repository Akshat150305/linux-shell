// NexusShell - executor.h
// Command execution and process management: runs a Pipeline using
// fork(), pipe(), dup2(), execvp() and waitpid().

#ifndef NEXUSSHELL_EXECUTOR_H
#define NEXUSSHELL_EXECUTOR_H

#include "builtins.h"
#include "parser.h"

// Run a pipeline and wait for it to finish. Returns its exit status, which is
// the status of the LAST command (as in bash):
//   0..255   normal exit code
//   128+N    killed by signal N
//   127      command not found
//   126      found but could not be executed
//   1        a redirection, pipe() or fork() failed
//
// A single built-in (no pipe, not background) runs inside the shell process so
// it can change shell state (cd, export, exit). Every other command, including
// built-ins that are part of a pipeline or a background job, runs in its own
// forked child.
//
// A background pipeline (`cmd &`) is started but not waited for: it is added to
// state.jobs, "[n] pid" is printed, and 0 is returned immediately.
int execute(ShellState& state, const Pipeline& pipeline);

#endif  // NEXUSSHELL_EXECUTOR_H
