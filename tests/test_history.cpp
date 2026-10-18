// Unit tests for history.cpp: add rules, file persistence, `!` expansion.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

#include <sys/stat.h>
#include <unistd.h>

#include "history.h"

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

using Vec = std::vector<std::string>;

static std::string slurp(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// expand() helper: returns "<kind>:<text>" for compact checks
static std::string ex(const std::string& line, const Vec& h) {
    std::string out, err;
    switch (history_expand(line, h, out, err)) {
        case Expansion::None: return "none:" + out;
        case Expansion::Expanded: return "ok:" + out;
        case Expansion::Error: return "err:" + err;
    }
    return "";
}

int main() {
    // --- history_add ---
    {
        Vec h;
        CHECK(history_add(h, "ls"));
        CHECK(!history_add(h, "ls"));        // consecutive duplicate
        CHECK(history_add(h, "pwd"));
        CHECK(history_add(h, "ls"));         // not consecutive: kept
        CHECK(!history_add(h, ""));
        CHECK(!history_add(h, "   \t"));
        CHECK(h.size() == 3);
        CHECK(history_add(h, "  indented"));   // leading blanks are kept as typed
        CHECK(h.back() == "  indented");
    }
    {
        Vec h;
        for (size_t i = 0; i < kHistoryLimit + 50; ++i) {
            history_add(h, "cmd" + std::to_string(i));
        }
        CHECK(h.size() == kHistoryLimit);
        CHECK(h.front() == "cmd50");                          // oldest dropped
        CHECK(h.back() == "cmd" + std::to_string(kHistoryLimit + 49));
    }

    // --- file persistence ---
    char tmpl[] = "/tmp/nexus_hist_testXXXXXX";
    int fd = mkstemp(tmpl);
    close(fd);
    std::string path = tmpl;
    {
        CHECK(history_append(path, "echo one"));
        CHECK(history_append(path, "echo \"two words\""));
        CHECK(slurp(path) == "echo one\necho \"two words\"\n");
        struct stat st;
        stat(path.c_str(), &st);
        CHECK((st.st_mode & 0777) == 0600);       // mkstemp makes 0600; append keeps it

        Vec h;
        CHECK(!history_load(path, h));            // not trimmed
        CHECK(h.size() == 2 && h[0] == "echo one" && h[1] == "echo \"two words\"");

        CHECK(history_rewrite(path, Vec{"a", "b"}));
        CHECK(slurp(path) == "a\nb\n");
        CHECK(history_rewrite(path, Vec{}));      // `history -c`
        CHECK(slurp(path).empty());
    }
    {
        Vec h;
        CHECK(!history_load("/tmp/nexus_no_such_file_xyz", h));   // missing file: empty, no error
        CHECK(h.empty());
    }
    {   // an over-long file is trimmed to the newest kHistoryLimit lines
        std::ofstream f(path, std::ios::trunc);
        for (size_t i = 0; i < kHistoryLimit + 10; ++i) {
            f << "line" << i << "\n";
        }
        f << "\n   \n";                            // blank lines are skipped
        f.close();
        Vec h;
        CHECK(history_load(path, h));              // true = caller should rewrite
        CHECK(h.size() == kHistoryLimit);
        CHECK(h.front() == "line10");
    }
    {   // a line with a trailing carriage return (file edited on Windows)
        std::ofstream f(path, std::ios::trunc);
        f << "ls\r\n";
        f.close();
        Vec h;
        history_load(path, h);
        CHECK(h.size() == 1);
    }
    unlink(path.c_str());

    // --- environment lookup of the path ---
    {
        setenv("NEXUSSHELL_HISTFILE", "/tmp/custom_hist", 1);
        CHECK(history_file_path() == "/tmp/custom_hist");
        unsetenv("NEXUSSHELL_HISTFILE");
        setenv("HOME", "/home/someone", 1);
        CHECK(history_file_path() == "/home/someone/.nexusshell_history");
        unsetenv("HOME");
        CHECK(history_file_path().empty());
    }

    // --- expansion ---
    {
        Vec h = {"ls -l", "echo hi", "pwd", "echo bye"};
        CHECK(ex("!!", h) == "ok:echo bye");
        CHECK(ex("!1", h) == "ok:ls -l");
        CHECK(ex("!4", h) == "ok:echo bye");
        CHECK(ex("!-1", h) == "ok:echo bye");
        CHECK(ex("!-3", h) == "ok:echo hi");
        CHECK(ex("!echo", h) == "ok:echo bye");      // newest match
        CHECK(ex("!ls", h) == "ok:ls -l");
        CHECK(ex("!! | wc", h) == "ok:echo bye | wc");  // trailing text kept
        CHECK(ex("!1 -a", h) == "ok:ls -l -a");
        CHECK(ex("  !!", h) == "ok:  echo bye");      // leading blanks kept
        CHECK(ex("!!x", h) == "ok:echo byex");

        CHECK(ex("!0", h) == "err:!0: event not found");
        CHECK(ex("!5", h) == "err:!5: event not found");
        CHECK(ex("!-5", h) == "err:!-5: event not found");
        CHECK(ex("!zzz", h) == "err:!zzz: event not found");
        CHECK(ex("!99999999999", h) == "err:!99999999999: event not found");

        // things that are NOT references
        CHECK(ex("echo !!", h) == "none:echo !!");     // only at the start of the line
        CHECK(ex("!", h) == "none:!");
        CHECK(ex("! ls", h) == "none:! ls");
        CHECK(ex("!= x", h) == "none:!= x");
        CHECK(ex("ls", h) == "none:ls");
        CHECK(ex("", h) == "none:");
        CHECK(ex("!!", Vec{}) == "err:!!: event not found");
        CHECK(ex("echo '!!'", h) == "none:echo '!!'");
    }

    std::cout << (g_total - g_failed) << "/" << g_total << " history tests passed" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
