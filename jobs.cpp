// NexusShell - jobs.cpp
// Phase 8: the job table for foreground AND background jobs.
//
// A job is a process group. Nothing in this file calls waitpid(): the SIGCHLD
// handler (signals.cpp) is the single reaper and records what happened to each
// child. refresh() applies those records to the table.
//
// Waiting for a foreground job uses the classic race-free pattern:
//
//     block SIGCHLD
//     loop:  apply recorded events; if the job is done or stopped -> leave
//            sigsuspend()   <- atomically "unblock SIGCHLD and sleep"
//
// Blocking first means a child cannot change state between "check" and "sleep"
// without the signal being delivered the moment sigsuspend() unblocks it.

#include "jobs.h"

#include <csignal>
#include <cstring>
#include <iomanip>
#include <iostream>

#include <sys/wait.h>
#include <unistd.h>

#include "signals.h"

// ---------------------------------------------------------------- Job ------

bool Job::done() const {
    for (const Process& p : procs) {
        if (!p.finished) {
            return false;
        }
    }
    return true;
}

bool Job::stopped() const {
    bool any_alive = false;
    for (const Process& p : procs) {
        if (p.finished) {
            continue;
        }
        any_alive = true;
        if (!p.stopped) {
            return false;  // someone is still running
        }
    }
    return any_alive;
}

int Job::exit_status() const {
    if (procs.empty() || !procs.back().finished) {
        return 0;
    }
    int status = procs.back().status;
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

int Job::term_signal() const {
    if (procs.empty() || !procs.back().finished) {
        return 0;
    }
    int status = procs.back().status;
    return WIFSIGNALED(status) ? WTERMSIG(status) : 0;
}

// ----------------------------------------------------------- JobTable ------

JobTable::JobTable() : owner_(getpid()) {}

bool JobTable::is_owner() const {
    // A forked copy (for example `jobs | cat` runs `jobs` in a child) has the
    // table, but the jobs are not ITS children.
    return getpid() == owner_;
}

bool JobTable::can_control() const {
    return is_owner() && job_control_active();
}

int JobTable::add(const std::vector<pid_t>& pids, pid_t pgid, const std::string& command,
                  bool foreground) {
    int id = 1;
    for (const Job& job : jobs_) {
        if (job.id >= id) {
            id = job.id + 1;
        }
    }

    Job job;
    job.id = id;
    job.pgid = pgid;
    job.command = command;
    job.foreground = foreground;
    for (pid_t pid : pids) {
        Process p;
        p.pid = pid;
        job.procs.push_back(p);
    }
    jobs_.push_back(job);
    return id;
}

Job* JobTable::find_mutable(int id) {
    for (Job& job : jobs_) {
        if (job.id == id) {
            return &job;
        }
    }
    return nullptr;
}

const Job* JobTable::find(int id) const {
    for (const Job& job : jobs_) {
        if (job.id == id) {
            return &job;
        }
    }
    return nullptr;
}

size_t JobTable::index_of(int id) const {
    for (size_t i = 0; i < jobs_.size(); ++i) {
        if (jobs_[i].id == id) {
            return i;
        }
    }
    return jobs_.size();
}

void JobTable::forget(int id) {
    size_t i = index_of(id);
    if (i < jobs_.size()) {
        jobs_.erase(jobs_.begin() + static_cast<long>(i));
    }
}

void JobTable::forget_done() {
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        it = (it->done() && !it->foreground) ? jobs_.erase(it) : it + 1;
    }
}

bool JobTable::apply_event(pid_t pid, int status) {
    for (Job& job : jobs_) {
        for (Process& p : job.procs) {
            if (p.pid != pid) {
                continue;
            }
            if (WIFSTOPPED(status)) {
                p.stopped = true;
            } else if (WIFCONTINUED(status)) {
                p.stopped = false;
                job.notified = false;  // may be announced again if it stops again
            } else {                   // exited or killed by a signal
                p.finished = true;
                p.stopped = false;
                p.status = status;
            }
            return true;
        }
    }
    return false;  // not one of ours
}

void JobTable::refresh() {
    if (!is_owner()) {
        return;
    }
    for (const ChildEvent& event : take_child_events()) {
        apply_event(event.pid, event.status);
    }
}

// ------------------------------------------------------------ display ------

char JobTable::marker(size_t index) const {
    if (index + 1 == jobs_.size()) {
        return '+';  // the current job: what a bare `fg` / `bg` picks
    }
    if (index + 2 == jobs_.size()) {
        return '-';
    }
    return ' ';
}

std::string JobTable::status_text(const Job& job) {
    if (job.done()) {
        int sig = job.term_signal();
        if (sig != 0) {
            const char* name = strsignal(sig);
            return name != nullptr ? name : "Signal " + std::to_string(sig);
        }
        int code = job.exit_status();
        return code != 0 ? "Exit " + std::to_string(code) : "Done";
    }
    return job.stopped() ? "Stopped" : "Running";
}

void JobTable::print_job(std::ostream& out, const Job& job, size_t index) const {
    bool running = !job.done() && !job.stopped();
    out << '[' << job.id << ']' << marker(index) << "  " << std::left << std::setw(11)
        << status_text(job) << job.command << (running ? " &" : "") << std::endl;
}

void JobTable::notify() {
    refresh();
    for (size_t i = 0; i < jobs_.size(); ++i) {
        Job& job = jobs_[i];
        if (job.foreground) {
            continue;
        }
        if (job.done()) {
            print_job(std::cerr, job, i);
        } else if (job.stopped() && !job.notified) {
            print_job(std::cerr, job, i);
            job.notified = true;
        }
    }
    forget_done();
}

void JobTable::list() {
    refresh();
    for (size_t i = 0; i < jobs_.size(); ++i) {
        print_job(std::cout, jobs_[i], i);
        if (jobs_[i].stopped()) {
            jobs_[i].notified = true;
        }
    }
    forget_done();
}

// ------------------------------------------------------------ control ------

void JobTable::resume(Job& job) {
    for (Process& p : job.procs) {
        if (!p.finished) {
            p.stopped = false;
        }
    }
    if (job.pgid > 0) {
        kill(-job.pgid, SIGCONT);  // a negative pid means "the whole group"
    }
}

int JobTable::run_in_foreground(int id, bool send_cont) {
    if (!is_owner()) {
        return 1;
    }
    Job* job = find_mutable(id);
    if (job == nullptr) {
        return 1;
    }
    job->foreground = true;
    job->notified = false;

    // Bringing back a stopped job: put its own terminal settings back first.
    if (send_cont && job->has_modes) {
        restore_terminal_modes(job->modes);
    }
    give_terminal_to(job->pgid);
    if (send_cont) {
        resume(*job);
    }

    {
        SigchldBlock block;
        for (;;) {
            refresh();
            if (job->done() || job->stopped()) {
                break;
            }
            sigsuspend(&block.previous());  // sleep until the next signal
        }
    }

    take_terminal_back();
    job->foreground = false;

    if (job->done()) {
        int status = job->exit_status();
        int sig = job->term_signal();
        if (sig == 0) {
            remember_shell_modes();  // e.g. `stty -echo` was the command: keep it
        } else {
            restore_shell_modes();   // killed: it may have left the terminal raw
        }
        if (job_control_active() && sig != 0 && sig != SIGPIPE) {
            if (sig == SIGINT) {
                std::cerr << std::endl;  // the terminal echoed ^C without a newline
            } else {
                const char* name = strsignal(sig);
                std::cerr << (name != nullptr ? name : "Signal") << std::endl;
            }
        }
        forget(id);
        return status;
    }

    // Stopped (Ctrl+Z): remember its terminal settings, take ours back, tell
    // the user, and keep the job in the table so fg / bg can find it.
    job->has_modes = save_terminal_modes(job->modes);
    restore_shell_modes();
    job->notified = true;
    std::cerr << std::endl;
    print_job(std::cerr, *job, index_of(id));
    return 128 + SIGTSTP;
}

void JobTable::run_in_background(int id) {
    if (!is_owner()) {
        return;
    }
    Job* job = find_mutable(id);
    if (job == nullptr) {
        return;
    }
    job->foreground = false;
    if (job->stopped()) {
        resume(*job);
    }
    std::cout << '[' << job->id << ']' << marker(index_of(id)) << ' ' << job->command
              << " &" << std::endl;
}

int JobTable::resolve(const std::string& spec) const {
    if (jobs_.empty()) {
        return 0;
    }
    std::string s = spec;
    if (!s.empty() && s[0] == '%') {
        s.erase(0, 1);
    }
    if (s.empty() || s == "%" || s == "+") {
        return jobs_.back().id;
    }
    if (s == "-") {
        return jobs_.size() >= 2 ? jobs_[jobs_.size() - 2].id : 0;
    }
    if (s.size() > 9 || s.find_first_not_of("0123456789") != std::string::npos) {
        return 0;
    }
    int id = std::stoi(s);
    return find(id) != nullptr ? id : 0;
}

bool JobTable::has_stopped() const {
    if (!is_owner()) {
        return false;
    }
    for (const Job& job : jobs_) {
        if (!job.done() && job.stopped()) {
            return true;
        }
    }
    return false;
}

void JobTable::hangup_stopped() {
    if (!is_owner()) {
        return;
    }
    for (const Job& job : jobs_) {
        if (!job.done() && job.stopped() && job.pgid > 0) {
            // A stopped process cannot act on SIGHUP until it runs again, so
            // follow it with SIGCONT.
            kill(-job.pgid, SIGHUP);
            kill(-job.pgid, SIGCONT);
        }
    }
}
