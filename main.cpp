// NexusShell - main.cpp
// Phase 1: Minimal interactive shell loop (Read -> Eval -> Print -> Loop)
//
// At this stage the shell does NOT execute real commands yet.
// It only proves the input loop is stable: it reads a line, and
// either exits (on "exit") or echoes back what it read.
// Real command execution (fork/exec) comes in Phase 2.

#include <iostream>
#include <string>

int main() {
    std::string input;

    while (true) {
        // --- Read ---
        std::cout << "nexus> ";      // temporary placeholder prompt
                                       // (real prompt with user@host:cwd comes in Phase 10)
        if (!std::getline(std::cin, input)) {
            // EOF (e.g. Ctrl+D) -> exit cleanly, like real shells do
            std::cout << std::endl;
            break;
        }

        // --- Eval (placeholder for now) ---
        if (input.empty()) {
            // Just an Enter press with nothing typed -> re-prompt, don't crash
            continue;
        }

        if (input == "exit") {
            break;
        }

        // --- Print (placeholder) ---
        std::cout << "You typed: " << input << std::endl;

        // --- Loop ---
    }

    return 0;
}
