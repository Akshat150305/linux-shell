// NexusShell - signals.h
// Signal handling and terminal ownership (job control).
//
// Who gets Ctrl+C / Ctrl+Z? The terminal driver sends SIGINT / SIGTSTP to the
// FOREGROUND PROCESS GROUP of the terminal, and to nobody else. So the shell
// puts every job in its own process group and hands the terminal to the group
// that should receive the keys (tcsetpgrp). The shell itself ignores Ctrl+Z
// and only notes Ctrl+C.

#ifndef NEXUSSHELL_SIGNALS_H
#define NEXUSSHELL_SIGNALS_H

#include <vector>

#include <sys/types.h>
#include <termios.h>

// One report from waitpid(): "child `pid` changed state, `status` says how"
// (exited, killed by a signal, stopped, or continued).
struct ChildEvent {
    pid_t pid;
    int status;
};

// Call once at startup. Installs the SIGCHLD handler. If stdin is a terminal
// it also turns on job control: own process group, owns the terminal, ignores
// Ctrl+Z, handles Ctrl+C.
void init_signals();

// True when job control is on (interactive shell that owns its terminal).
bool job_control_active();

// Call in a freshly forked child: restore default signal behaviour and an
// empty signal mask, so the program behaves normally (Ctrl+C kills it).
void reset_child_signals();

// True if Ctrl+C reached the shell since the last call (and clears the flag).
bool take_sigint();

// Collect every child state change recorded so far. Blocks SIGCHLD while it
// works, so it cannot race with the handler.
std::vector<ChildEvent> take_child_events();

// RAII: blocks SIGCHLD for the lifetime of the object. Used around fork() so
// the handler cannot report a child before the job table knows about it.
class SigchldBlock {
public:
    SigchldBlock();
    ~SigchldBlock();
    SigchldBlock(const SigchldBlock&) = delete;
    SigchldBlock& operator=(const SigchldBlock&) = delete;

    // The signal mask from before we blocked (what sigsuspend should use).
    const sigset_t& previous() const { return previous_; }

private:
    sigset_t previous_;
};

// ---- terminal ownership (no-ops when job control is off) ----
void give_terminal_to(pid_t pgid);               // that group gets Ctrl+C / Ctrl+Z
void take_terminal_back();                       // the shell's group gets it again
bool save_terminal_modes(termios& out);          // remember the terminal settings
void restore_terminal_modes(const termios& in);  // put settings back
void restore_shell_modes();                      // the settings the shell remembers
void remember_shell_modes();                     // adopt the terminal's current settings
                                                 // (so `stty -echo` as a command sticks)

#endif  // NEXUSSHELL_SIGNALS_H
