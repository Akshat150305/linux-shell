// NexusShell - prompt.cpp
// Phase 10: the Linux-style prompt, exit-status indicator and welcome banner.
//
// Colours are ANSI escape sequences: ESC [ <codes> m selects a style and
// ESC [ 0 m resets it. They take no screen columns, which is why the line
// editor (which repaints the prompt from column 0) needs no width bookkeeping.

#include "prompt.h"

#include <climits>
#include <cstdlib>
#include <cstring>

#include <pwd.h>
#include <unistd.h>

namespace {

const char* const kReset = "\033[0m";
const char* const kBoldGreen = "\033[1;32m";
const char* const kBoldBlue = "\033[1;34m";
const char* const kBoldRed = "\033[1;31m";
const char* const kBoldCyan = "\033[1;36m";
const char* const kDim = "\033[2m";

// Wrap `text` in a style if colour is on.
std::string paint(const char* style, const std::string& text, bool color) {
    return color ? std::string(style) + text + kReset : text;
}

}  // namespace

std::string abbreviate_home(const std::string& cwd, const std::string& home) {
    if (home.empty() || home == "/") {
        return cwd;
    }
    if (cwd == home) {
        return "~";
    }
    if (cwd.size() > home.size() && cwd.compare(0, home.size(), home) == 0 &&
        cwd[home.size()] == '/') {
        return "~" + cwd.substr(home.size());
    }
    return cwd;
}

std::string format_prompt(const PromptInfo& info) {
    std::string text;
    if (info.status != 0) {
        text += paint(kBoldRed, "[" + std::to_string(info.status) + "]", info.color) + " ";
    }
    text += paint(kBoldGreen, info.user + "@" + info.host, info.color);
    text += ":";
    text += paint(kBoldBlue, abbreviate_home(info.cwd, info.home), info.color);
    text += info.root ? " # " : " $ ";
    return text;
}

bool use_color() {
    if (!isatty(STDOUT_FILENO)) {
        return false;
    }
    const char* no_color = std::getenv("NO_COLOR");
    if (no_color != nullptr && *no_color != '\0') {
        return false;  // https://no-color.org: any non-empty value turns colour off
    }
    const char* term = std::getenv("TERM");
    return term == nullptr || std::strcmp(term, "dumb") != 0;
}

std::string build_prompt(int last_status) {
    PromptInfo info;

    // The account database is authoritative; $USER can be set to anything.
    if (const passwd* entry = getpwuid(geteuid())) {
        info.user = entry->pw_name;
    } else if (const char* user = std::getenv("USER")) {
        info.user = user;
    } else {
        info.user = "user";
    }

    char host[HOST_NAME_MAX + 1];
    if (gethostname(host, sizeof(host)) == 0) {
        host[HOST_NAME_MAX] = '\0';
        info.host = host;
    } else {
        info.host = "localhost";
    }

    char cwd[PATH_MAX];
    info.cwd = getcwd(cwd, sizeof(cwd)) != nullptr ? cwd : "?";  // "?": directory was deleted

    const char* home = std::getenv("HOME");
    info.home = home != nullptr ? home : "";
    info.status = last_status;
    info.root = geteuid() == 0;
    info.color = use_color();
    return format_prompt(info);
}

std::string welcome_banner(bool color) {
    std::string title = std::string("NexusShell ") + kShellVersion;
    return paint(kBoldCyan, title, color) + " - a Unix shell written from scratch in C++\n" +
           paint(kDim, "Type 'help' for the built-in commands, 'exit' to leave.", color) + "\n";
}
