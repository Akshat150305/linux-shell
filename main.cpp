// NexusShell - main.cpp
// Phase 9: the shell loop. Before each prompt, report finished or stopped
// jobs; then read a line and:
//
//   tokenize()        lexer.cpp     quotes, escapes, $VAR, ~, operators
//   parse_pipeline()  parser.cpp    tokens -> commands (argv + redirections)
//   execute()         executor.cpp  built-ins, fork/exec, pipes, background
//
// read_line() (input.cpp) is a line editor on a terminal. It reports Ctrl+C
// (Interrupted) and Ctrl+D (Eof) separately. History is loaded from, and each
// accepted command appended to, ~/.nexusshell_history, but only when stdin is
// a terminal, so scripts never touch the user's history.

#include "builtins.h"
#include "executor.h"
#include "history.h"
#include "input.h"
#include "lexer.h"
#include "parser.h"
#include "signals.h"

#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

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

    if (isatty(STDIN_FILENO)) {
        state.history_file = history_file_path();
        if (!state.history_file.empty() && history_load(state.history_file, state.history)) {
            history_rewrite(state.history_file, state.history);  // file was too long: trim it
        }
    }

    while (state.running) {
        // --- Report jobs that finished or stopped since the last prompt ---
        state.jobs.notify();
        take_sigint();  // forget a Ctrl+C that arrived while we were not reading

        // --- Read ---
        ReadResult result = read_line("nexus> ", state.history, input);
        if (result == ReadResult::Interrupted) {
            continue;  // Ctrl+C: the editor already printed a fresh line
        }
        if (result == ReadResult::Eof) {
            if (state.jobs.has_stopped() && !state.exit_warned) {
                std::cerr << "There are stopped jobs." << std::endl;
                state.exit_warned = true;  // a second Ctrl+D leaves
                continue;
            }
            break;
        }

        // Blank line: just prompt again.
        if (input.find_first_not_of(" \t") == std::string::npos) {
            continue;
        }

        // --- History: expand !! / !N / !text, then record the line ---
        std::string expanded;
        std::string history_error;
        Expansion kind = history_expand(input, state.history, expanded, history_error);
        if (kind == Expansion::Error) {
            std::cerr << "nexus: " << history_error << std::endl;
            state.last_status = 1;
            continue;
        }
        if (kind == Expansion::Expanded) {
            std::cout << expanded << std::endl;  // show what is about to run
            input = expanded;
        }
        if (history_add(state.history, input) && !state.history_file.empty()) {
            history_append(state.history_file, input);
        }

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
