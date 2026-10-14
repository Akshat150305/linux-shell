// NexusShell - parser.h
// Parser: turns the lexer's tokens into a Command: the argument list plus the
// redirections to set up before running it.

#ifndef NEXUSSHELL_PARSER_H
#define NEXUSSHELL_PARSER_H

#include <string>
#include <vector>

#include "lexer.h"
#include "redirect.h"

struct Command {
    Args argv;                               // program name and arguments
    std::vector<Redirection> redirections;   // in the order they were written
};

// Build a Command from tokens. Redirections may appear anywhere in the line
// (`> out echo hi` is valid) and are removed from argv.
//
// Returns false and sets `error` on a syntax error: a redirection operator
// with no file name after it, or an unsupported `2>&1`-style target.
bool parse_command(const std::vector<Token>& tokens, Command& command,
                   std::string& error);

#endif  // NEXUSSHELL_PARSER_H
