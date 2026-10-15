// NexusShell - lexer.h
// Lexer/tokenizer: turns one input line into tokens, applying quoting,
// backslash escapes, variable expansion and ~ expansion.
//
// Programs never see quotes: `echo "a b"` reaches echo as {"echo", "a b"}.
// Operators (< > >> 2> |) are recognized only when unquoted, so `echo ">"`
// prints a character while `echo >` is a redirection.

#ifndef NEXUSSHELL_LEXER_H
#define NEXUSSHELL_LEXER_H

#include <string>
#include <vector>

using Args = std::vector<std::string>;

// One lexical unit: either a finished word (quotes/expansions already
// applied) or an operator.
struct Token {
    std::string text;           // the word, or the operator: "<" ">" ">>" "2>" "|"
    bool is_operator = false;
};

// Tokenize `line` into `tokens`.
//
// last_status  value substituted for $?
// tokens       (output) the resulting tokens; cleared first
// error        (output) human-readable message when false is returned
//
// Returns true on success, false on a syntax error (unterminated quote,
// trailing backslash, malformed ${...}). A line with no tokens (blank, or only
// unset variables) succeeds with an empty `tokens`.
bool tokenize(const std::string& line, int last_status, std::vector<Token>& tokens,
              std::string& error);

#endif  // NEXUSSHELL_LEXER_H
