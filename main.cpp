// NexusShell - main.cpp
// Phase 4: shell loop + lexer + external commands + built-ins.
//
// Each line is tokenized (lexer.cpp: quotes, escapes, $VAR, ~), then run as
// either a built-in (inside the shell process, see builtins.cpp) or an
// external program (in a forked child).
//
// Not yet supported:
//   - pipes, redirection, background jobs (-> Phases 5-7)
//   - signal handling (-> Phase 8)

#include "builtins.h"
#include "lexer.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <sys/wait.h>  // waitpid, WIFEXITED, WEXITSTATUS, ...
#include <unistd.h>    // fork, execvp, _exit

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

        // Blank line: just prompt again.
        if (input.find_first_not_of(" \t") == std::string::npos) {
            continue;
        }
        state.history.push_back(input);  // record the raw line, as bash does

        // --- Parse ---
        Args args;
        std::string error;
        if (!tokenize(input, state.last_status, args, error)) {
            std::cerr << "nexus: syntax error: " << error << std::endl;
            state.last_status = 2;  // same status bash uses for syntax errors
            continue;
        }
        if (args.empty()) {
            continue;  // e.g. a line containing only an unset $VARIABLE
        }

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
