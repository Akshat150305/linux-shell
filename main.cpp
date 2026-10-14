// NexusShell - main.cpp
// Phase 3: shell loop + external commands (fork/execvp/waitpid) + built-ins.
//
// Each line is either a built-in (runs inside the shell process, see
// builtins.cpp) or an external program (runs in a forked child).
//
// Still intentionally naive:
//   - input is split on whitespace only (no quotes/escapes/$VAR -> Phase 4)
//   - no pipes, redirection or background jobs (-> Phases 5-7)

#include "builtins.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/wait.h>  // waitpid, WIFEXITED, WEXITSTATUS, ...
#include <unistd.h>    // fork, execvp, _exit

// Split a line into whitespace-separated words.
// "ls   -l  /tmp" -> {"ls", "-l", "/tmp"}
static Args split_whitespace(const std::string& line) {
    std::istringstream stream(line);
    Args words;
    std::string word;
    while (stream >> word) {
        words.push_back(word);
    }
    return words;
}

// Run one external command and wait for it to finish.
// Returns the shell-style exit status:
//   0..255   normal exit code of the child
//   128+N    child was killed by signal N
//   127      command not found
//   126      found but could not be executed (e.g. permission denied)
static int run_command(const Args& args) {
    // execvp wants a NULL-terminated array of char*, not std::string.
    std::vector<char*> argv;
    for (const std::string& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    // Flush our buffered output first so it can't be duplicated or
    // reordered relative to the child's output.
    std::cout.flush();

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "nexus: fork failed: " << std::strerror(errno) << std::endl;
        return 1;
    }

    if (pid == 0) {
        // ---- CHILD ----
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

    // ---- PARENT ----
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;  // interrupted by a signal; just retry
        }
        std::cerr << "nexus: waitpid failed: " << std::strerror(errno) << std::endl;
        return 1;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

int main() {
    ShellState state;
    std::string input;

    while (state.running) {
        // --- Read ---
        std::cout << "nexus> ";
        if (!std::getline(std::cin, input)) {
            std::cout << std::endl;  // Ctrl+D / EOF
            break;
        }

        // --- Parse (naive) ---
        Args args = split_whitespace(input);
        if (args.empty()) {
            continue;  // blank or whitespace-only line
        }
        state.history.push_back(input);

        // --- Eval ---
        // Built-ins run in this very process (no fork); everything else is
        // an external program run in a child.
        if (is_builtin(args[0])) {
            state.last_status = run_builtin(state, args);
        } else {
            state.last_status = run_command(args);
        }
    }

    return state.last_status;
}
