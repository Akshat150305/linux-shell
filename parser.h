// NexusShell - parser.h
// Parser: turns the lexer's tokens into a Pipeline: one or more Commands, each
// with its argument list and the redirections to set up before running it.

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

// A line of commands joined by '|'. A line without a pipe is a Pipeline with
// one Command; a blank line (no tokens) is a Pipeline with none.
struct Pipeline {
    std::vector<Command> commands;
};

// Build one Command from tokens that contain no '|'. Redirections may appear
// anywhere (`> out echo hi` is valid) and are removed from argv.
//
// Returns false and sets `error` on a syntax error: a redirection operator
// with no file name after it, or an unsupported `2>&1`-style target.
bool parse_command(const std::vector<Token>& tokens, Command& command,
                   std::string& error);

// Split tokens at '|' and parse each part with parse_command().
// In a real pipeline (two or more commands) every command needs a program name:
// `| cat`, `echo |`, `a || b` and `> f | cat` are syntax errors.
bool parse_pipeline(const std::vector<Token>& tokens, Pipeline& pipeline,
                    std::string& error);

#endif  // NEXUSSHELL_PARSER_H
