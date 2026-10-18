// NexusShell - input.h
// Reading one line of input, with line editing and history recall when stdin
// is a terminal.

#ifndef NEXUSSHELL_INPUT_H
#define NEXUSSHELL_INPUT_H

#include <string>
#include <vector>

enum class ReadResult {
    Line,         // `line` holds a command line (without the newline)
    Eof,          // Ctrl+D on an empty line, or end of input
    Interrupted,  // Ctrl+C while typing: the line was discarded
};

// Print `prompt` and read one line.
//
// On a terminal this is a small line editor: the terminal is switched out of
// canonical mode while we read, so we see every key (arrows, Ctrl+A, ...) and
// echo it ourselves. Up/Down walk through `history`. The terminal is put back
// to normal before returning, so commands never run in raw mode.
//
// When stdin is not a terminal (scripts, pipes) it is a plain getline().
ReadResult read_line(const std::string& prompt, const std::vector<std::string>& history,
                     std::string& line);

#endif  // NEXUSSHELL_INPUT_H
