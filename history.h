// NexusShell - history.h
// Command history: an in-memory list plus a persistent file.
//
// Everything here is a plain function on a std::vector<std::string>, so it can
// be unit-tested without a terminal.

#ifndef NEXUSSHELL_HISTORY_H
#define NEXUSSHELL_HISTORY_H

#include <cstddef>
#include <string>
#include <vector>

constexpr size_t kHistoryLimit = 1000;  // entries kept in memory and in the file

// Where the history file lives: $NEXUSSHELL_HISTFILE if set, else
// $HOME/.nexusshell_history. Empty string if neither is available.
std::string history_file_path();

// Add a line to the in-memory list. Returns false (and adds nothing) for a
// blank line or a repeat of the previous entry. Drops the oldest entry when
// the list is longer than kHistoryLimit.
bool history_add(std::vector<std::string>& history, const std::string& line);

// Read the file into `history` (the last kHistoryLimit lines). Returns true if
// the file had to be trimmed, so the caller can rewrite it. A missing file is
// not an error: the history is simply empty.
bool history_load(const std::string& path, std::vector<std::string>& history);

// Append one line to the file with open(O_APPEND). Returns false on I/O error.
bool history_append(const std::string& path, const std::string& line);

// Replace the whole file with `history` (used after trimming and by `history -c`).
bool history_rewrite(const std::string& path, const std::vector<std::string>& history);

enum class Expansion {
    None,      // the line is not a history reference; `out` == `line`
    Expanded,  // `out` is the line with the reference replaced (show it to the user)
    Error,     // reference not found; `error` says why
};

// Expand a leading history reference:
//   !!   previous command      !N   command number N      !-N  N-th from the end
//   !ab  newest command starting with "ab"
// Text after the reference is kept ("!! | wc" -> "<previous> | wc").
// Only a reference at the START of the line is recognised.
Expansion history_expand(const std::string& line, const std::vector<std::string>& history,
                         std::string& out, std::string& error);

#endif  // NEXUSSHELL_HISTORY_H
