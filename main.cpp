// NexusShell - main.cpp
// Phase 6: the shell loop. Read a line, then:
//
//   tokenize()        lexer.cpp     quotes, escapes, $VAR, ~, operators
//   parse_pipeline()  parser.cpp    tokens -> commands (argv + redirections)
//   execute()         executor.cpp  built-ins, fork/exec, pipes, waitpid
//
// Not yet supported:
//   - background jobs (-> Phase 7)
//   - signal handling (-> Phase 8)

#include "builtins.h"
#include "executor.h"
#include "lexer.h"
#include "parser.h"

#include <iostream>
#include <string>
#include <vector>

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

        // --- Eval ---
        state.last_status = execute(state, pipeline);
    }

    return state.last_status;
}
