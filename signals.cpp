// NexusShell - signals.cpp
// Phase 8: signals, process groups and terminal control.
//
// The SIGCHLD design ("one reaper"):
//   * The SIGCHLD handler is the only place that calls waitpid(-1, ...).
//   * It does the bare minimum, which is all that is safe inside a signal
//     handler: waitpid() and store (pid, status) in a fixed array.
//   * The main code later takes those events (take_child_events) and updates
//     the job table. Nothing else calls waitpid, so nobody can steal another
//     part's child status.

#include "signals.h"

#include <cerrno>
#include <csignal>
#include <cstring>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr int kMaxEvents = 1024;

// WNOHANG:    never block, just report "nothing more to collect".
// WUNTRACED:  also report children that were STOPPED (Ctrl+Z), not only dead.
// WCONTINUED: also report children that were resumed with SIGCONT.
constexpr int kWaitFlags = WNOHANG | WUNTRACED | WCONTINUED;

ChildEvent g_events[kMaxEvents];
volatile sig_atomic_t g_event_count = 0;  // written by the handler
volatile sig_atomic_t g_sigint = 0;       // set by the SIGINT handler

bool g_job_control = false;
int g_tty_fd = -1;         // our own duplicate of the terminal, immune to `< file`
pid_t g_shell_pgid = 0;
termios g_shell_modes;     // terminal settings the shell started with

// Runs when a child changes state. Only async-signal-safe calls in here:
// waitpid(), plain array writes, and saving/restoring errno (a handler must
// not clobber the errno of the code it interrupted).
void on_sigchld(int) {
    int saved_errno = errno;
    int status = 0;
    pid_t pid = 0;
    while (g_event_count < kMaxEvents && (pid = waitpid(-1, &status, kWaitFlags)) > 0) {
        g_events[g_event_count].pid = pid;
        g_events[g_event_count].status = status;
        g_event_count = g_event_count + 1;
    }
    errno = saved_errno;
}

void on_sigint(int) {
    g_sigint = 1;
}

// sigaction() is the reliable way to install a handler (signal() behaviour
// differs between systems). `handler` may also be SIG_IGN or SIG_DFL.
void set_handler(int sig, void (*handler)(int), int flags) {
    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = flags;
    sigaction(sig, &action, nullptr);
}

}  // namespace

void init_signals() {
    // SA_RESTART: if SIGCHLD interrupts a slow call such as read(), the kernel
    // restarts that call instead of failing it with EINTR.
    set_handler(SIGCHLD, on_sigchld, SA_RESTART);

    if (!isatty(STDIN_FILENO)) {
        return;  // script or pipe: no job control; Ctrl+C kills the shell as usual
    }

    // If we were started in the background we are not allowed to use the
    // terminal. Stop ourselves with SIGTTIN until the user brings us forward.
    set_handler(SIGTTIN, SIG_DFL, 0);
    for (;;) {
        pid_t foreground = tcgetpgrp(STDIN_FILENO);
        if (foreground < 0) {
            return;  // no controlling terminal: leave job control off
        }
        if (foreground == getpgrp()) {
            break;
        }
        kill(-getpgrp(), SIGTTIN);
    }

    // The shell must survive keys meant for its jobs.
    set_handler(SIGINT, on_sigint, 0);  // no SA_RESTART: it must interrupt getline()
    set_handler(SIGQUIT, SIG_IGN, 0);
    set_handler(SIGTSTP, SIG_IGN, 0);   // Ctrl+Z at the prompt does nothing
    set_handler(SIGTTIN, SIG_IGN, 0);
    set_handler(SIGTTOU, SIG_IGN, 0);   // needed so tcsetpgrp() works from a background group

    // Our own process group: Ctrl+C aimed at a job must never reach the shell.
    // (EPERM just means we already lead a session; that is fine too.)
    setpgid(0, 0);
    g_shell_pgid = getpgrp();

    // Keep a private copy of the terminal descriptor (>= 10, close-on-exec),
    // so job control still works while a built-in has stdin redirected.
    g_tty_fd = fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 10);
    if (g_tty_fd < 0) {
        g_tty_fd = STDIN_FILENO;
    }

    tcsetpgrp(g_tty_fd, g_shell_pgid);
    tcgetattr(g_tty_fd, &g_shell_modes);
    g_job_control = true;
}

bool job_control_active() {
    return g_job_control;
}

void reset_child_signals() {
    // A handler is reset by exec, but SIG_IGN is INHERITED through exec, and a
    // forked built-in never execs. So put every signal we touched back to
    // the default explicitly.
    const int touched[] = {SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU, SIGCHLD};
    for (int sig : touched) {
        set_handler(sig, SIG_DFL, 0);
    }
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, nullptr);  // fork() copied our blocked SIGCHLD
}

bool take_sigint() {
    bool was_set = g_sigint != 0;
    g_sigint = 0;
    return was_set;
}

std::vector<ChildEvent> take_child_events() {
    SigchldBlock block;  // the handler cannot run while we copy the array

    std::vector<ChildEvent> events(g_events, g_events + g_event_count);
    g_event_count = 0;

    // Catch-up: if the array was full, the handler left some children
    // uncollected. Collect them here, still with SIGCHLD blocked.
    int status = 0;
    pid_t pid = 0;
    while ((pid = waitpid(-1, &status, kWaitFlags)) > 0) {
        events.push_back(ChildEvent{pid, status});
    }
    return events;
}

SigchldBlock::SigchldBlock() {
    sigset_t chld;
    sigemptyset(&chld);
    sigaddset(&chld, SIGCHLD);
    sigprocmask(SIG_BLOCK, &chld, &previous_);
}

SigchldBlock::~SigchldBlock() {
    sigprocmask(SIG_SETMASK, &previous_, nullptr);
}

void give_terminal_to(pid_t pgid) {
    if (g_job_control) {
        tcsetpgrp(g_tty_fd, pgid);
    }
}

void take_terminal_back() {
    give_terminal_to(g_shell_pgid);
}

bool save_terminal_modes(termios& out) {
    return g_job_control && tcgetattr(g_tty_fd, &out) == 0;
}

void restore_terminal_modes(const termios& in) {
    if (g_job_control) {
        tcsetattr(g_tty_fd, TCSADRAIN, &in);
    }
}

void restore_shell_modes() {
    restore_terminal_modes(g_shell_modes);
}

void remember_shell_modes() {
    if (g_job_control) {
        tcgetattr(g_tty_fd, &g_shell_modes);
    }
}
