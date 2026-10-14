// NexusShell - main.cpp
// Phase 5: shell loop + lexer + parser + redirection + external commands +
// built-ins.
//
// Each line is tokenized (lexer.cpp: quotes, escapes, $VAR, ~), parsed into a
// Command (parser.cpp: argv + redirections), then run as either a built-in
// (inside the shell process, see builtins.cpp) or an external program (in a
// forked child). Redirections (redirect.cpp) are set up with open()/dup2().
//
// Not yet supported:
//   - pipes and background jobs (-> Phases 6-7)
//   - signal handling (-> Phase 8)

#include "builtins.h"
#include "lexer.h"
#include "parser.h"
#include "redirect.h"

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
static int run_command(const Command& command) {
    // execvp wants a NULL-terminated array of char*, not std::string.
    std::vector<char*> argv;
    for (const std::string& arg : command.argv) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    // Flush our buffered output first so it can't be duplicated or
    // reordered relative to the child's output.
    flush_output();

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "nexus: fork failed: " << std::strerror(errno) << std::endl;
        return 1;
    }

    if (pid == 0) {
        // ---- CHILD ----
        // Rewire stdin/stdout/stderr here, in the child only, so the shell's
        // own descriptors stay untouched. exec keeps them open.
        std::string redirect_error;
        if (!apply_redirections(command.redirections, redirect_error)) {
            std::cerr << "nexus: " << redirect_error << std::endl;
            _exit(1);  // the command is not run when a redirection fails
        }

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

        // --- Parse: line -> tokens -> Command ---
        std::vector<Token> tokens;
        Command command;
        std::string error;
        if (!tokenize(input, state.last_status, tokens, error) ||
            !parse_command(tokens, command, error)) {
            std::cerr << "nexus: syntax error: " << error << std::endl;
            state.last_status = 2;  // same status bash uses for syntax errors
            continue;
        }
        if (command.argv.empty() && command.redirections.empty()) {
            continue;  // e.g. a line containing only an unset $VARIABLE
        }

        // --- Eval ---
        if (command.argv.empty() || is_builtin(command.argv[0])) {
            // Built-ins run in this very process (no fork), so there is no
            // child to rewire: RedirectScope redirects, then puts the original
            // descriptors back. A line with only redirections (`> file`) has no
            // command but still creates/truncates the file, as in bash.
            RedirectScope scope;
            std::string redirect_error;
            if (!scope.apply(command.redirections, redirect_error)) {
                scope.restore();  // report the error on the real stderr
                std::cerr << "nexus: " << redirect_error << std::endl;
                state.last_status = 1;
            } else if (command.argv.empty()) {
                state.last_status = 0;
            } else {
                state.last_status = run_builtin(state, command.argv);
            }
        } else {
            state.last_status = run_command(command);
        }
    }

    return state.last_status;
}
