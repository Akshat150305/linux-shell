// NexusShell - jobs.h
// Job table. A job is one command line (a single command or a whole pipeline)
// running as one PROCESS GROUP. Foreground and background jobs are both in the
// table; the shell learns what happened to their processes from SIGCHLD events
// (see signals.h), never by polling.

#ifndef NEXUSSHELL_JOBS_H
#define NEXUSSHELL_JOBS_H

#include <iosfwd>
#include <string>
#include <vector>

#include <sys/types.h>
#include <termios.h>

struct Process {
    pid_t pid = 0;
    bool finished = false;  // exited or was killed
    bool stopped = false;   // suspended (Ctrl+Z, SIGSTOP, SIGTTIN, ...)
    int status = 0;         // raw wait() status, valid when finished
};

struct Job {
    int id = 0;                    // the number in [1], [2], ...
    pid_t pgid = 0;                // process group: signals go to -pgid
    std::vector<Process> procs;    // one per pipeline stage
    std::string command;           // the command line, for display
    bool foreground = false;       // the shell is waiting for it
    bool notified = false;         // a "Stopped" notice was already printed
    bool has_modes = false;        // `modes` holds the terminal settings at stop time
    termios modes{};               // restored when the job is brought back with fg

    bool done() const;             // every process has finished
    bool stopped() const;          // not done, and every live process is stopped
    int exit_status() const;       // the last stage's status, shell style
    int term_signal() const;       // signal that killed the last stage, or 0
};

class JobTable {
public:
    JobTable();

    // Register a started pipeline. Returns its job number.
    int add(const std::vector<pid_t>& pids, pid_t pgid, const std::string& command,
            bool foreground);

    // Apply the recorded SIGCHLD events to the table.
    void refresh();

    // Apply ONE wait() status to the process `pid`. Returns false if no job
    // has that process. (refresh() calls this; the unit tests call it directly.)
    bool apply_event(pid_t pid, int status);

    // Before each prompt: refresh, then print "Done" / "Stopped" notices for
    // background jobs on stderr and forget the finished ones.
    void notify();

    // The `jobs` built-in.
    void list();

    // `fg`: give the job the terminal (continuing it if `send_cont`) and wait
    // until it finishes or stops. Returns its exit status (148 if stopped).
    int run_in_foreground(int id, bool send_cont);

    // `bg`: continue a stopped job in the background.
    void run_in_background(int id);

    // "%2", "2", "%%", "%-" or "" (the current job) -> job id, or 0 if none.
    int resolve(const std::string& spec) const;
    const Job* find(int id) const;

    bool has_stopped() const;
    void hangup_stopped();   // on shell exit: SIGHUP + SIGCONT to stopped jobs

    // True only in the real shell process of an interactive session. (A forked
    // copy, for example `fg | cat`, has the table but not the children.)
    bool can_control() const;

    bool empty() const { return jobs_.empty(); }
    size_t size() const { return jobs_.size(); }

private:
    Job* find_mutable(int id);
    bool is_owner() const;  // are we the shell process that forked these jobs?
    size_t index_of(int id) const;
    void forget(int id);
    void forget_done();
    static void resume(Job& job);
    char marker(size_t index) const;  // '+' most recent job, '-' the one before
    static std::string status_text(const Job& job);
    void print_job(std::ostream& out, const Job& job, size_t index) const;

    std::vector<Job> jobs_;
    pid_t owner_;  // the shell process; only it may act on these children
};

#endif  // NEXUSSHELL_JOBS_H
