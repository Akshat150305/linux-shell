// NexusShell - jobs.cpp
// Phase 7: the job table for background pipelines.
//
// Finished children are collected by POLLING: waitpid(pid, &status, WNOHANG)
// returns 0 if the child is still running, its PID if it just finished (and
// frees the zombie), or -1 if there is no such child. Phase 8 replaces the
// polling with a SIGCHLD handler.

#include "jobs.h"

#include <cerrno>
#include <cstring>
#include <iomanip>
#include <iostream>

#include <sys/wait.h>
#include <unistd.h>

JobTable::JobTable() : owner_(getpid()) {}

int JobTable::add(const std::vector<pid_t>& pids, const std::string& command) {
    Job job;
    job.id = next_id_++;
    job.pids = pids;
    job.last_pid = pids.empty() ? 0 : pids.back();
    job.command = command;
    jobs_.push_back(job);
    return job.id;
}

void JobTable::poll() {
    // A forked child inherits a copy of this table (for example `jobs | cat`
    // runs `jobs` in a child) but the jobs are not ITS children, so it must not
    // wait for them: it would wrongly conclude they are gone.
    if (getpid() != owner_) {
        return;
    }

    for (Job& job : jobs_) {
        if (job.done) {
            continue;
        }
        for (auto it = job.pids.begin(); it != job.pids.end();) {
            int status = 0;
            pid_t result = waitpid(*it, &status, WNOHANG);
            if (result == 0) {
                ++it;  // still running
                continue;
            }
            if (result < 0 && errno == EINTR) {
                ++it;  // interrupted; look again at the next poll
                continue;
            }
            if (result == *it && *it == job.last_pid) {
                if (WIFEXITED(status)) {
                    job.exit_code = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    job.term_signal = WTERMSIG(status);
                }
            }
            // result == pid: collected just now. result < 0: no such child
            // any more. Either way this stage is finished.
            it = job.pids.erase(it);
        }
        if (job.pids.empty()) {
            job.done = true;
        }
    }
}

char JobTable::marker(size_t index) const {
    if (index + 1 == jobs_.size()) {
        return '+';
    }
    if (index + 2 == jobs_.size()) {
        return '-';
    }
    return ' ';
}

std::string JobTable::status_text(const Job& job) {
    if (!job.done) {
        return "Running";
    }
    if (job.term_signal != 0) {
        const char* name = strsignal(job.term_signal);
        return name != nullptr ? name : "Signal " + std::to_string(job.term_signal);
    }
    if (job.exit_code != 0) {
        return "Exit " + std::to_string(job.exit_code);
    }
    return "Done";
}

void JobTable::print_job(std::ostream& out, const Job& job, size_t index) const {
    out << '[' << job.id << ']' << marker(index) << "  " << std::left << std::setw(11)
        << status_text(job) << job.command << (job.done ? "" : " &") << std::endl;
}

void JobTable::forget_done() {
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        it = it->done ? jobs_.erase(it) : it + 1;
    }
    if (jobs_.empty()) {
        next_id_ = 1;  // like bash: numbering restarts when no jobs are left
    }
}

void JobTable::notify_done() {
    poll();
    for (size_t i = 0; i < jobs_.size(); ++i) {
        if (jobs_[i].done) {
            print_job(std::cerr, jobs_[i], i);
        }
    }
    forget_done();
}

void JobTable::list() {
    poll();
    for (size_t i = 0; i < jobs_.size(); ++i) {
        print_job(std::cout, jobs_[i], i);
    }
    forget_done();
}
