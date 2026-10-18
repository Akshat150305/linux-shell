// NexusShell - builtins.h
// Built-in commands: commands that run INSIDE the shell process (no fork),
// because they must change or read the shell's own state.

#ifndef NEXUSSHELL_BUILTINS_H
#define NEXUSSHELL_BUILTINS_H

#include <string>
#include <vector>

#include "jobs.h"
#include "lexer.h"  // Args: built-ins receive the lexer's output

// State shared between the main loop and the built-ins.
struct ShellState {
    bool running = true;                  // main loop runs while this is true
    int last_status = 0;                  // exit status of the last command
    std::vector<std::string> history;     // commands entered (oldest first)
    std::string history_file;             // persistent file; empty = not saved (scripts)
    JobTable jobs;                        // foreground and background jobs
    bool exit_warned = false;             // the last command warned about stopped jobs
    bool exit_confirmed = false;          // ...so this `exit` may proceed
};

// True if `name` is one of the built-in commands.
bool is_builtin(const std::string& name);

// Run a built-in. args[0] must be a built-in name (check is_builtin first).
// Returns the command's exit status.
int run_builtin(ShellState& state, const Args& args);

#endif  // NEXUSSHELL_BUILTINS_H
