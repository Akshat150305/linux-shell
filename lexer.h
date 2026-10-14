// NexusShell - lexer.h
// Lexer/tokenizer: turns one input line into the final list of arguments,
// applying quoting, backslash escapes, variable expansion and ~ expansion.
//
// Programs never see quotes: `echo "a b"` reaches echo as {"echo", "a b"}.

#ifndef NEXUSSHELL_LEXER_H
#define NEXUSSHELL_LEXER_H

#include <string>
#include <vector>

using Args = std::vector<std::string>;

// Tokenize `line` into `words`.
//
// last_status  value substituted for $?
// words        (output) the resulting arguments; cleared first
// error        (output) human-readable message when false is returned
//
// Returns true on success, false on a syntax error (unterminated quote,
// trailing backslash, malformed ${...}). A line with no words (blank, or only
// unset variables) succeeds with an empty `words`.
bool tokenize(const std::string& line, int last_status, Args& words,
              std::string& error);

#endif  // NEXUSSHELL_LEXER_H
