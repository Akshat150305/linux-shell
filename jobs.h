// NexusShell - jobs.h
// Job table: remembers background pipelines so the shell can report them
// (`jobs`), notice when they finish, and collect (reap) them with waitpid().
// A child that has exited but was never waited for stays a zombie.

#ifndef NEXUSSHELL_JOBS_H
#define NEXUSSHELL_JOBS_H

#include <iosfwd>
#include <string>
#include <vector>

#include <sys/types.h>

struct Job {
    int id = 0;                 // the number in [1], [2], ...
    std::vector<pid_t> pids;    // stages of the pipeline that are still running
    pid_t last_pid = 0;         // last stage: its status is the job's status
    std::string command;        // the command line, for display
    bool done = false;          // every stage has been collected
    int exit_code = 0;          // valid when done and term_signal == 0
    int term_signal = 0;        // signal that killed the last stage, or 0
};

class JobTable {
public:
    JobTable();

    // Register a started background pipeline. Returns its job number.
    int add(const std::vector<pid_t>& pids, const std::string& command);

    // Collect finished children (non-blocking), then print every finished job
    // as "[1]+  Done   cmd" on stderr and forget it. Called before each prompt.
    void notify_done();

    // The `jobs` built-in: collect finished children, then list all jobs on
    // stdout. A finished job is shown once and then forgotten.
    void list();

    bool empty() const { return jobs_.empty(); }
    size_t size() const { return jobs_.size(); }

private:
    void poll();
    char marker(size_t index) const;  // '+' most recent job, '-' the one before
    static std::string status_text(const Job& job);
    void print_job(std::ostream& out, const Job& job, size_t index) const;
    void forget_done();

    std::vector<Job> jobs_;
    int next_id_ = 1;
    pid_t owner_;  // the shell process; only it may wait for these children
};

#endif  // NEXUSSHELL_JOBS_H
