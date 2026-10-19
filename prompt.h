// NexusShell - prompt.h
// The interactive prompt and welcome banner:
//
//     akshat@nexus:~/projects/linux-shell $
//     [127] akshat@nexus:~ $          <- red [N] when the last command failed

#ifndef NEXUSSHELL_PROMPT_H
#define NEXUSSHELL_PROMPT_H

#include <string>

constexpr const char* kShellVersion = "1.0";

// Everything the prompt shows, gathered in one place so that formatting is a
// pure function (and unit-testable).
struct PromptInfo {
    std::string user;
    std::string host;
    std::string cwd;     // absolute path, not yet abbreviated
    std::string home;    // $HOME, used to print the directory as "~"
    int status = 0;      // exit status of the previous command
    bool root = false;   // prompt ends in '#' instead of '$'
    bool color = false;  // emit ANSI escape sequences
};

// "/home/me" -> "~", "/home/me/src" -> "~/src"; other paths are unchanged.
// "/home/meow" is NOT under "/home/me".
std::string abbreviate_home(const std::string& cwd, const std::string& home);

// Build the prompt text from `info`.
std::string format_prompt(const PromptInfo& info);

// Colour only if stdout is a terminal, NO_COLOR is unset and TERM is not "dumb".
bool use_color();

// Read the live values (getpwuid, gethostname, getcwd, ...) and format the prompt.
std::string build_prompt(int last_status);

// The text printed once when an interactive shell starts.
std::string welcome_banner(bool color);

#endif  // NEXUSSHELL_PROMPT_H
