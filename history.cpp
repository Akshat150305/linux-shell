// NexusShell - history.cpp
// Phase 9: in-memory history, persistence in ~/.nexusshell_history, `!` expansion.
//
// The file is plain text, one command per line. New commands are APPENDED as
// they are accepted: with O_APPEND the kernel moves to the end of the file and
// writes in one step, so a crash loses nothing and two shells can share the file.

#include "history.h"

#include <cctype>
#include <cstdlib>
#include <fstream>

#include <fcntl.h>
#include <unistd.h>

std::string history_file_path() {
    const char* custom = std::getenv("NEXUSSHELL_HISTFILE");
    if (custom != nullptr && *custom != '\0') {
        return custom;
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::string(home) + "/.nexusshell_history";
    }
    return "";
}

bool history_add(std::vector<std::string>& history, const std::string& line) {
    if (line.find_first_not_of(" \t\r\n") == std::string::npos) {
        return false;  // blank
    }
    if (!history.empty() && history.back() == line) {
        return false;  // same as the previous command
    }
    history.push_back(line);
    if (history.size() > kHistoryLimit) {
        history.erase(history.begin(),
                      history.begin() + static_cast<long>(history.size() - kHistoryLimit));
    }
    return true;
}

bool history_load(const std::string& path, std::vector<std::string>& history) {
    history.clear();
    std::ifstream file(path);
    if (!file) {
        return false;
    }
    bool trimmed = false;
    std::string line;
    while (std::getline(file, line)) {
        if (line.find_first_not_of(" \t\r") == std::string::npos) {
            continue;
        }
        history.push_back(line);
        if (history.size() > kHistoryLimit) {
            history.erase(history.begin());
            trimmed = true;
        }
    }
    return trimmed;
}

bool history_append(const std::string& path, const std::string& line) {
    // 0600: a history file can contain secrets typed on the command line.
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd < 0) {
        return false;
    }
    std::string text = line + "\n";  // one write() call keeps the line in one piece
    ssize_t written = write(fd, text.data(), text.size());
    close(fd);
    return written == static_cast<ssize_t>(text.size());
}

bool history_rewrite(const std::string& path, const std::vector<std::string>& history) {
    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        return false;
    }
    for (const std::string& line : history) {
        file << line << '\n';
    }
    return static_cast<bool>(file);
}

Expansion history_expand(const std::string& line, const std::vector<std::string>& history,
                         std::string& out, std::string& error) {
    out = line;
    error.clear();

    size_t bang = line.find_first_not_of(" \t");
    if (bang == std::string::npos || line[bang] != '!') {
        return Expansion::None;
    }
    // "!" alone, "! cmd" and "!=" are not references.
    size_t next = bang + 1;
    if (next >= line.size() || line[next] == ' ' || line[next] == '\t' || line[next] == '=') {
        return Expansion::None;
    }

    std::string found;
    size_t end = next;  // where the reference ends

    if (line[next] == '!') {  // !!
        end = next + 1;
        if (history.empty()) {
            error = "!!: event not found";
            return Expansion::Error;
        }
        found = history.back();
    } else if (std::isdigit(static_cast<unsigned char>(line[next])) ||
               (line[next] == '-' && next + 1 < line.size() &&
                std::isdigit(static_cast<unsigned char>(line[next + 1])))) {
        bool from_end = (line[next] == '-');
        size_t digits = from_end ? next + 1 : next;
        end = digits;
        while (end < line.size() && std::isdigit(static_cast<unsigned char>(line[end]))) {
            ++end;
        }
        std::string number_text = line.substr(digits, end - digits);
        std::string label = line.substr(bang, end - bang);
        if (number_text.size() > 9) {
            error = label + ": event not found";
            return Expansion::Error;
        }
        size_t n = static_cast<size_t>(std::stoul(number_text));
        if (n == 0 || n > history.size()) {
            error = label + ": event not found";
            return Expansion::Error;
        }
        found = from_end ? history[history.size() - n] : history[n - 1];
    } else {  // !prefix
        while (end < line.size() && line[end] != ' ' && line[end] != '\t') {
            ++end;
        }
        std::string prefix = line.substr(next, end - next);
        bool match = false;
        for (size_t i = history.size(); i-- > 0;) {
            if (history[i].compare(0, prefix.size(), prefix) == 0) {
                found = history[i];
                match = true;
                break;
            }
        }
        if (!match) {
            error = "!" + prefix + ": event not found";
            return Expansion::Error;
        }
    }

    out = line.substr(0, bang) + found + line.substr(end);
    return Expansion::Expanded;
}
