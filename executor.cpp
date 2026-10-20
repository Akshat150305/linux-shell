// NexusShell - executor.cpp
// Phase 6: pipelines. One forked child per stage, joined by pipe()s.
// Phase 8: every pipeline is a job with its own process group; the terminal is
// handed to a foreground job, and waiting goes through the job table.
//
//   cmd1 | cmd2 | cmd3
//
//   cmd1 --stdout--> [pipe A] --stdin--> cmd2 --stdout--> [pipe B] --stdin--> cmd3
//
// The rule that prevents hangs: a reader sees end-of-file only when EVERY copy
// of the pipe's write end is closed. So each process closes every pipe
// descriptor it does not use, and the parent closes its copies right after
// each fork().

#include "executor.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <stdio_ext.h>  // __fpurge (glibc)
#include <unistd.h>

#include "redirect.h"
#include "signals.h"

namespace {

// Make `to` refer to the same file as `from`, then drop `from`.
void move_fd(int from, int to) {
    if (from == to) {
        fcntl(from, F_SETFD, 0);  // already the right number: just survive exec
        return;
    }
    dup2(from, to);
    close(from);
}

// Does this command redirect descriptor `fd` itself (for example `< file`)?
bool redirects_fd(const Command& command, int fd) {
    for (const Redirection& r : command.redirections) {
        if (r.fd == fd) {
            return true;
        }
    }
    return false;
}

// Everything one child does before becoming (or running) its command.
// Never returns: it execs, or exits.
//   in_fd       pipe read end to use as stdin, or -1
//   out_fd      pipe write end to use as stdout, or -1
//   unused_fd   the read end of the pipe we write into; this stage must not
//               keep it open, or it would be a stray reader. -1 if none.
//   null_stdin  first stage of a background job: read /dev/null instead of the
//               terminal, so the job cannot steal the shell's input
[[noreturn]] void run_stage(ShellState& state, const Command& command, int in_fd,
                            int out_fd, int unused_fd, bool null_stdin) {
    if (in_fd >= 0) {
        move_fd(in_fd, STDIN_FILENO);
    } else if (null_stdin && !redirects_fd(command, STDIN_FILENO)) {
        int null_fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (null_fd >= 0) {
            move_fd(null_fd, STDIN_FILENO);
        }
    }
    if (out_fd >= 0) {
        move_fd(out_fd, STDOUT_FILENO);
    }
    if (unused_fd >= 0) {
        close(unused_fd);
    }

    // Explicit redirections come after the pipe wiring, so they win:
    // `echo hi > f | cat` writes to f and leaves cat with an empty input.
    std::string error;
    if (!apply_redirections(command.redirections, error)) {
        std::cerr << "nexus: " << error << std::endl;
        _exit(1);  // the command is not run when a redirection fails
    }

    // A built-in in a pipeline runs in this child: its output goes into the
    // pipe, and its effect on shell state (cd, export, exit) is discarded.
    if (is_builtin(command.argv[0])) {
        int status = run_builtin(state, command.argv);
        flush_output();  // _exit() skips the normal flush
        _exit(status);
    }

    // execvp wants a NULL-terminated array of char*, not std::string.
    std::vector<char*> argv;
    for (const std::string& arg : command.argv) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    execvp(argv[0], argv.data());

    // execvp only returns on failure. Save errno before doing any I/O,
    // because printing can overwrite it.
    int saved_errno = errno;
    if (saved_errno == ENOENT) {
        std::cerr << "nexus: " << argv[0] << ": command not found" << std::endl;
        _exit(127);
    }
    std::cerr << "nexus: " << argv[0] << ": " << std::strerror(saved_errno)
              << std::endl;
    _exit(126);  // _exit, not exit: don't run the parent's atexit/flush logic
}

// One built-in (or a line of only redirections) with no pipe: run it inside the
// shell process. There is no child to rewire, so RedirectScope redirects the
// shell's own descriptors and restores them afterwards.
int run_in_shell(ShellState& state, const Command& command) {
    RedirectScope scope;
    std::string error;
    if (!scope.apply(command.redirections, error)) {
        scope.restore();  // report the error on the real stderr
        std::cerr << "nexus: " << error << std::endl;
        return 1;
    }
    if (command.argv.empty()) {
        return 0;  // `> file`: the file was created/truncated, nothing to run
    }
    int status = run_builtin(state, command.argv);

    // Push the built-in's output out NOW, while the redirection is still in
    // place, so a failed write (disk full, closed pipe, ...) is noticed here.
    // Without this the failure would leave std::cout in a permanent error state
    // and silence every later message the shell prints.
    bool failed = std::fflush(stdout) != 0;
    int saved_errno = errno;  // fflush() sets it; printing below could change it
    failed = failed || std::ferror(stdout) || !std::cout;
    if (failed) {
        __fpurge(stdout);   // drop the unsent bytes: they must not reach the real stdout later
        clearerr(stdout);
        std::cout.clear();
        std::cerr << "nexus: " << command.argv[0] << ": write error: "
                  << std::strerror(saved_errno) << std::endl;
        return 1;
    }
    return status;
}

// Child side of job control: join the job's process group and, for a
// foreground job, take the terminal. The parent does the same after fork():
// whichever runs first wins, so there is no window where the child could read
// the terminal as a background process (and be stopped by SIGTTIN).
//   pgid == 0 means "I am the first stage: start a new group led by me".
void join_process_group(pid_t pgid, bool foreground) {
    if (!job_control_active()) {
        return;
    }
    setpgid(0, pgid);
    if (foreground) {
        give_terminal_to(getpgrp());
    }
}

}  // namespace

int execute(ShellState& state, const Pipeline& pipeline) {
    const std::vector<Command>& commands = pipeline.commands;
    if (commands.empty()) {
        return state.last_status;
    }

    // A single built-in runs in the shell itself, unless it is a background job:
    // that must be a child, because the shell does not wait for it.
    if (commands.size() == 1 && !pipeline.background &&
        (commands[0].argv.empty() || is_builtin(commands[0].argv[0]))) {
        return run_in_shell(state, commands[0]);
    }

    // Flush our buffered output first so no child inherits a copy of it and
    // prints it a second time.
    flush_output();

    std::vector<pid_t> pids;
    pid_t pgid = 0;       // process group of the job = pid of its first stage
    int prev_read = -1;   // read end of the previous stage's pipe
    bool failed = false;
    bool foreground = !pipeline.background;
    int id = 0;

    {
        // SIGCHLD stays blocked from the first fork() until the job is in the
        // table. Otherwise a fast child could exit and its report be looked up
        // before the table knew about it.
        SigchldBlock block;

        for (size_t i = 0; i < commands.size(); ++i) {
            bool last = (i + 1 == commands.size());

            // Pipes are made one at a time, so the parent never holds more
            // than two pipe ends. O_CLOEXEC closes them automatically on exec,
            // as a safety net on top of the explicit close() calls.
            int fds[2] = {-1, -1};  // fds[0] = read end, fds[1] = write end
            if (!last && pipe2(fds, O_CLOEXEC) < 0) {
                std::cerr << "nexus: pipe failed: " << std::strerror(errno) << std::endl;
                failed = true;
                break;
            }

            pid_t pid = fork();
            if (pid < 0) {
                std::cerr << "nexus: fork failed: " << std::strerror(errno) << std::endl;
                if (!last) {
                    close(fds[0]);
                    close(fds[1]);
                }
                failed = true;
                break;
            }

            if (pid == 0) {
                // ---- CHILD ----
                join_process_group(pgid, foreground);
                reset_child_signals();  // default signals, empty mask
                run_stage(state, commands[i], prev_read, last ? -1 : fds[1],
                          last ? -1 : fds[0], pipeline.background && i == 0);
            }

            // ---- PARENT ----
            if (pgid == 0) {
                pgid = pid;
            }
            if (job_control_active()) {
                setpgid(pid, pgid);  // the child does it too; doing both avoids a race
                if (foreground && pids.empty()) {
                    give_terminal_to(pgid);
                }
            }
            pids.push_back(pid);
            if (prev_read >= 0) {
                close(prev_read);  // the child now owns its input end
            }
            if (!last) {
                close(fds[1]);  // MUST close: otherwise the next stage never sees EOF
                prev_read = fds[0];
            } else {
                prev_read = -1;
            }
        }

        if (prev_read >= 0) {
            close(prev_read);  // only left open if the loop stopped early
        }
        if (!pids.empty()) {
            id = state.jobs.add(pids, pgid, pipeline.text, foreground);
        }
    }  // SIGCHLD unblocked here; any report that arrived is now delivered

    if (pids.empty()) {
        return 1;
    }

    // Background: do NOT wait. The SIGCHLD handler reaps the children when they
    // end, and the job table reports it before the next prompt.
    if (!foreground) {
        std::cout << '[' << id << "] " << pids.back() << std::endl;
        return failed ? 1 : 0;
    }

    // Foreground: wait through the job table. It returns when the job has
    // finished (its status is the last stage's) or has been stopped (148).
    int status = state.jobs.run_in_foreground(id, false);
    return failed ? 1 : status;
}
