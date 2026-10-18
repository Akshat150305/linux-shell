// NexusShell - main.cpp
// Phase 8: the shell loop. Before each prompt, report finished or stopped
// jobs; then read a line and:
//
//   tokenize()        lexer.cpp     quotes, escapes, $VAR, ~, operators
//   parse_pipeline()  parser.cpp    tokens -> commands (argv + redirections)
//   execute()         executor.cpp  built-ins, fork/exec, pipes, background
//
// Ctrl+C at the prompt interrupts getline() (SIGINT is handled without
// SA_RESTART), which is how we tell it apart from Ctrl+D (end of file).

#include "builtins.h"
#include "executor.h"
#include "lexer.h"
#include "parser.h"
#include "signals.h"

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

// The command line as shown by `jobs`: trimmed, without the trailing '&'.
static std::string job_text(std::string line) {
    const char* blanks = " \t\n";
    line.erase(line.find_last_not_of(blanks) + 1);
    if (!line.empty() && line.back() == '&') {
        line.pop_back();
        line.erase(line.find_last_not_of(blanks) + 1);
    }
    line.erase(0, line.find_first_not_of(blanks));
    return line;
}

int main() {
    init_signals();
    ShellState state;
    std::string input;

    while (state.running) {
        // --- Report jobs that finished or stopped since the last prompt ---
        state.jobs.notify();
        take_sigint();  // forget a Ctrl+C that arrived while we were not reading

        // --- Read ---
        std::cout << "nexus> ";
        if (!std::getline(std::cin, input)) {
            if (take_sigint()) {
                // Ctrl+C interrupted the read: not end-of-file. Fresh prompt.
                std::cin.clear();
                clearerr(stdin);
                std::cout << std::endl;
                continue;
            }
            std::cout << std::endl;  // Ctrl+D / EOF
            if (state.jobs.has_stopped() && !state.exit_warned) {
                std::cerr << "There are stopped jobs." << std::endl;
                state.exit_warned = true;  // a second Ctrl+D leaves
                std::cin.clear();
                clearerr(stdin);
                continue;
            }
            break;
        }

        // Blank line: just prompt again.
        if (input.find_first_not_of(" \t") == std::string::npos) {
            continue;
        }
        state.history.push_back(input);  // record the raw line, as bash does

        // --- Parse: line -> tokens -> Pipeline ---
        std::vector<Token> tokens;
        Pipeline pipeline;
        std::string error;
        if (!tokenize(input, state.last_status, tokens, error) ||
            !parse_pipeline(tokens, pipeline, error)) {
            std::cerr << "nexus: syntax error: " << error << std::endl;
            state.last_status = 2;  // same status bash uses for syntax errors
            continue;
        }
        if (pipeline.commands.empty()) {
            continue;  // e.g. a line containing only an unset $VARIABLE
        }
        pipeline.text = job_text(input);

        // --- Eval ---
        state.exit_confirmed = state.exit_warned;  // `exit` twice in a row leaves
        state.exit_warned = false;
        state.last_status = execute(state, pipeline);
    }

    state.jobs.hangup_stopped();  // stopped jobs would otherwise be stuck forever
    return state.last_status;
}
