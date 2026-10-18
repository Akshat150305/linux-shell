// Unit tests for the job table's state machine. No processes are started:
// raw wait() statuses are fed in through apply_event(), exactly as the SIGCHLD
// path would.

#include <csignal>
#include <iostream>

#include "jobs.h"

static int g_total = 0;
static int g_failed = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_total;                                                               \
        if (!(cond)) {                                                           \
            ++g_failed;                                                          \
            std::cerr << "FAIL line " << __LINE__ << ": " #cond << std::endl;    \
        }                                                                        \
    } while (0)

// Raw statuses, built the way the kernel encodes them.
static int exited(int code) { return code << 8; }
static int killed(int sig) { return sig; }
static int stopped_by(int sig) { return (sig << 8) | 0x7f; }
static const int kContinued = 0xffff;

int main() {
    // --- Job predicates ---
    {
        JobTable t;
        int id = t.add({100, 101}, 100, "a | b", true);
        const Job* j = t.find(id);
        CHECK(id == 1);
        CHECK(!j->done() && !j->stopped());

        t.apply_event(100, exited(0));
        CHECK(!j->done());                       // stage 2 still runs
        t.apply_event(101, exited(3));
        CHECK(j->done());
        CHECK(j->exit_status() == 3);            // the LAST stage decides
        CHECK(j->term_signal() == 0);
    }
    {
        JobTable t;
        int id = t.add({200}, 200, "sleep 9", false);
        const Job* j = t.find(id);
        t.apply_event(200, killed(SIGKILL));
        CHECK(j->done());
        CHECK(j->term_signal() == SIGKILL);
        CHECK(j->exit_status() == 128 + SIGKILL);
    }
    {
        JobTable t;
        int id = t.add({300}, 300, "sleep 9", true);
        const Job* j = t.find(id);
        CHECK(t.apply_event(300, stopped_by(SIGTSTP)));
        CHECK(j->stopped() && !j->done());
        CHECK(t.has_stopped());
        t.apply_event(300, kContinued);
        CHECK(!j->stopped() && !j->done());
        CHECK(!t.has_stopped());
    }
    {   // a pipeline with one stage finished and the other stopped is stopped
        JobTable t;
        int id = t.add({400, 401}, 400, "a | b", true);
        const Job* j = t.find(id);
        t.apply_event(400, exited(0));
        t.apply_event(401, stopped_by(SIGTTIN));
        CHECK(j->stopped() && !j->done());
    }
    {   // one stage stopped, one still running: not "stopped"
        JobTable t;
        int id = t.add({500, 501}, 500, "a | b", true);
        t.apply_event(500, stopped_by(SIGTSTP));
        CHECK(!t.find(id)->stopped());
    }

    // --- events for unknown children are ignored ---
    {
        JobTable t;
        t.add({600}, 600, "x", false);
        CHECK(!t.apply_event(9999, exited(0)));
    }

    // --- job numbers and resolve() ---
    {
        JobTable t;
        int a = t.add({1}, 1, "one", false);
        int b = t.add({2}, 2, "two", false);
        int c = t.add({3}, 3, "three", false);
        CHECK(a == 1 && b == 2 && c == 3);
        CHECK(t.resolve("") == 3);       // current job = most recent
        CHECK(t.resolve("%") == 3);
        CHECK(t.resolve("%%") == 3);
        CHECK(t.resolve("%+") == 3);
        CHECK(t.resolve("%-") == 2);
        CHECK(t.resolve("-") == 2);
        CHECK(t.resolve("%1") == 1);
        CHECK(t.resolve("2") == 2);
        CHECK(t.resolve("%9") == 0);     // no such job
        CHECK(t.resolve("%abc") == 0);
        CHECK(t.resolve("%1x") == 0);
        CHECK(t.resolve("99999999999") == 0);
    }
    {
        JobTable t;
        CHECK(t.resolve("") == 0);       // empty table
        t.add({1}, 1, "only", false);
        CHECK(t.resolve("%-") == 0);     // there is no "previous" job
    }

    // --- finished background jobs are forgotten after being listed ---
    {
        JobTable t;
        t.add({700}, 700, "x", false);
        t.apply_event(700, exited(0));
        CHECK(t.size() == 1);
        std::cout.setstate(std::ios::failbit);   // list() prints; keep the test output clean
        t.list();
        std::cout.clear();
        CHECK(t.empty());
        CHECK(t.add({701}, 701, "y", false) == 1);  // numbering restarts
    }

    std::cout.width(0);
    std::cout << (g_total - g_failed) << "/" << g_total << " job table tests passed" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
