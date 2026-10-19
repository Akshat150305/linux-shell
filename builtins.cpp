// NexusShell - builtins.cpp
// Phase 3: cd, pwd, echo, exit, clear, help, history, export. (+ jobs, fg, bg)

#include "builtins.h"

#include "history.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <unordered_map>

#include <unistd.h>  // chdir, getcwd

extern char** environ;  // this process's environment: array of "NAME=value"

namespace {

using BuiltinFn = int (*)(ShellState&, const Args&);

// A valid variable name: [A-Za-z_][A-Za-z0-9_]*
bool is_valid_name(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    unsigned char first = static_cast<unsigned char>(name[0]);
    if (!std::isalpha(first) && first != '_') {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        unsigned char u = static_cast<unsigned char>(c);
        return std::isalnum(u) || u == '_';
    });
}

// cd [dir]   |   cd   |   cd -
// chdir() changes THIS process's working directory. That is why cd cannot be
// an external program: a forked child would change only its own directory.
int builtin_cd(ShellState&, const Args& args) {
    if (args.size() > 2) {
        std::cerr << "nexus: cd: too many arguments" << std::endl;
        return 1;
    }

    std::string target;
    bool print_target = false;  // `cd -` prints the directory it moves to

    if (args.size() == 1) {
        const char* home = std::getenv("HOME");
        if (home == nullptr || *home == '\0') {
            std::cerr << "nexus: cd: HOME not set" << std::endl;
            return 1;
        }
        target = home;
    } else if (args[1] == "-") {
        const char* old = std::getenv("OLDPWD");
        if (old == nullptr || *old == '\0') {
            std::cerr << "nexus: cd: OLDPWD not set" << std::endl;
            return 1;
        }
        target = old;
        print_target = true;
    } else {
        target = args[1];
    }

    // Remember where we were (getcwd can fail if the directory was deleted).
    char before[PATH_MAX];
    bool have_before = getcwd(before, sizeof(before)) != nullptr;

    if (chdir(target.c_str()) != 0) {
        std::cerr << "nexus: cd: " << target << ": " << std::strerror(errno)
                  << std::endl;
        return 1;
    }

    if (have_before) {
        setenv("OLDPWD", before, 1);
    }
    char after[PATH_MAX];
    if (getcwd(after, sizeof(after)) != nullptr) {
        setenv("PWD", after, 1);
        if (print_target) {
            std::cout << after << std::endl;
        }
    }
    return 0;
}

// pwd: print the current working directory.
int builtin_pwd(ShellState&, const Args&) {
    char buffer[PATH_MAX];
    if (getcwd(buffer, sizeof(buffer)) == nullptr) {
        std::cerr << "nexus: pwd: " << std::strerror(errno) << std::endl;
        return 1;
    }
    std::cout << buffer << std::endl;
    return 0;
}

// echo [-n] [args...]: print arguments separated by single spaces.
int builtin_echo(ShellState&, const Args& args) {
    size_t i = 1;
    bool newline = true;
    if (i < args.size() && args[i] == "-n") {
        newline = false;
        ++i;
    }
    for (size_t first = i; i < args.size(); ++i) {
        if (i > first) {
            std::cout << ' ';
        }
        std::cout << args[i];
    }
    if (newline) {
        std::cout << '\n';
    }
    std::cout.flush();
    return 0;
}

// exit [n]: stop the shell with status n (default: status of last command).
// Like bash: a non-numeric argument exits with 2; too many arguments is an
// error and the shell keeps running.
int builtin_exit(ShellState& state, const Args& args) {
    if (args.size() > 2) {
        std::cerr << "nexus: exit: too many arguments" << std::endl;
        return 1;
    }
    // Leaving would abandon stopped jobs: warn once; a second `exit` leaves.
    if (state.jobs.has_stopped() && !state.exit_confirmed) {
        std::cerr << "There are stopped jobs." << std::endl;
        state.exit_warned = true;
        return 1;
    }
    if (args.size() == 1) {
        state.running = false;
        return state.last_status;
    }

    char* end = nullptr;
    errno = 0;
    long code = std::strtol(args[1].c_str(), &end, 10);
    state.running = false;
    if (end == args[1].c_str() || *end != '\0' || errno == ERANGE) {
        std::cerr << "nexus: exit: " << args[1] << ": numeric argument required"
                  << std::endl;
        return 2;
    }
    return static_cast<int>(code & 0xFF);  // exit statuses are 8 bits
}

// clear: move the cursor home and erase the screen using ANSI escape codes.
int builtin_clear(ShellState&, const Args&) {
    std::cout << "\033[H\033[2J" << std::flush;
    return 0;
}

// help: list the built-in commands.
int builtin_help(ShellState&, const Args&) {
    std::cout <<
        "NexusShell built-in commands\n"
        "\n"
        "  cd [dir]           change directory (cd = $HOME, cd - = previous)\n"
        "  pwd                print the current directory\n"
        "  echo [-n] [text]   print text (-n: no trailing newline)\n"
        "  export [NAME=val]  set an environment variable (no args: list all)\n"
        "  history [N|-c]     show the last N commands (all by default); -c clears\n"
        "  jobs               list running and stopped jobs\n"
        "  fg [%n]            bring a job to the foreground (resumes it if stopped)\n"
        "  bg [%n]            resume a stopped job in the background\n"
        "  clear              clear the screen\n"
        "  help               show this message\n"
        "  exit [n]           leave the shell with status n\n"
        "\n"
        "Running programs\n"
        "\n"
        "  cmd arg ...        any other command is searched for in $PATH\n"
        "  cmd &              run in the background\n"
        "  a | b | c          pipeline\n"
        "  > f  >> f  < f     redirect stdout (truncate / append) and stdin\n"
        "  2> f               redirect stderr\n"
        "  \"..\" '..' \\x       quoting and escaping; $VAR ${VAR} $? $$ ~ expand\n"
        "\n"
        "Keys\n"
        "\n"
        "  Ctrl+C             interrupt the running command (or discard the typed line)\n"
        "  Ctrl+Z             stop the running command; resume with fg or bg\n"
        "  Ctrl+D             exit on an empty line\n"
        "  Up / Down          previous / next command from the history\n"
        "  Left Right Home End  Ctrl+A Ctrl+E Ctrl+U Ctrl+K Ctrl+W Ctrl+L  edit the line\n"
        "  !! !N !-N !text    run an earlier command again (at the start of a line)\n"
        "\n"
        "The prompt shows [N] in red when the last command exited with status N.\n";
    return 0;
}

// history [N]   history -c
// Numbers are positions in the list, so `!N` refers to the number shown here.
int builtin_history(ShellState& state, const Args& args) {
    if (args.size() > 2) {
        std::cerr << "nexus: history: too many arguments" << std::endl;
        return 1;
    }

    size_t first = 0;  // index of the first entry to print
    if (args.size() == 2) {
        if (args[1] == "-c") {  // clear memory and the file
            state.history.clear();
            if (!state.history_file.empty() && !history_rewrite(state.history_file, {})) {
                std::cerr << "nexus: history: cannot write " << state.history_file << std::endl;
                return 1;
            }
            return 0;
        }
        if (args[1].empty() || args[1].size() > 9 ||
            args[1].find_first_not_of("0123456789") != std::string::npos) {
            std::cerr << "nexus: history: " << args[1] << ": numeric argument required"
                      << std::endl;
            return 1;
        }
        size_t count = static_cast<size_t>(std::stoul(args[1]));  // show the last N
        first = state.history.size() > count ? state.history.size() - count : 0;
    }

    for (size_t i = first; i < state.history.size(); ++i) {
        std::cout << std::setw(5) << (i + 1) << "  " << state.history[i] << '\n';
    }
    std::cout.flush();
    return 0;
}

// export [NAME=value ...]
// setenv() edits this process's environment; every child we fork afterwards
// inherits it through execvp. With no arguments, list the environment.
int builtin_export(ShellState&, const Args& args) {
    if (args.size() == 1) {
        std::vector<std::string> lines;
        for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
            std::string text = *entry;
            size_t eq = text.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            lines.push_back("export " + text.substr(0, eq) + "=\"" +
                            text.substr(eq + 1) + "\"");
        }
        std::sort(lines.begin(), lines.end());
        for (const std::string& line : lines) {
            std::cout << line << '\n';
        }
        std::cout.flush();
        return 0;
    }

    int status = 0;
    for (size_t i = 1; i < args.size(); ++i) {
        size_t eq = args[i].find('=');
        std::string name = (eq == std::string::npos) ? args[i] : args[i].substr(0, eq);

        if (!is_valid_name(name)) {
            std::cerr << "nexus: export: `" << args[i] << "': not a valid identifier"
                      << std::endl;
            status = 1;
            continue;
        }
        if (eq == std::string::npos) {
            continue;  // `export NAME` with no value: nothing to change
        }
        if (setenv(name.c_str(), args[i].c_str() + eq + 1, 1) != 0) {
            std::cerr << "nexus: export: " << std::strerror(errno) << std::endl;
            status = 1;
        }
    }
    return status;
}

// jobs: list running and stopped jobs.
int builtin_jobs(ShellState& state, const Args&) {
    state.jobs.list();
    return 0;
}

// Look up the job a fg/bg argument refers to. Prints an error and returns 0
// if job control is off or there is no such job.
int pick_job(ShellState& state, const char* name, const Args& args) {
    if (!state.jobs.can_control()) {
        std::cerr << "nexus: " << name << ": no job control" << std::endl;
        return 0;
    }
    if (args.size() > 2) {
        std::cerr << "nexus: " << name << ": too many arguments" << std::endl;
        return 0;
    }
    state.jobs.refresh();
    std::string spec = args.size() == 2 ? args[1] : "";
    int id = state.jobs.resolve(spec);
    if (id == 0) {
        std::cerr << "nexus: " << name << ": " << (spec.empty() ? "current" : spec)
                  << ": no such job" << std::endl;
    }
    return id;
}

// fg [%n]: give the job the terminal and wait for it.
int builtin_fg(ShellState& state, const Args& args) {
    int id = pick_job(state, "fg", args);
    if (id == 0) {
        return 1;
    }
    std::cout << state.jobs.find(id)->command << std::endl;
    return state.jobs.run_in_foreground(id, true);
}

// bg [%n]: let a stopped job continue in the background.
int builtin_bg(ShellState& state, const Args& args) {
    int id = pick_job(state, "bg", args);
    if (id == 0) {
        return 1;
    }
    const Job* job = state.jobs.find(id);
    if (job->done()) {
        std::cerr << "nexus: bg: job has terminated" << std::endl;
        return 1;
    }
    if (!job->stopped()) {
        std::cerr << "nexus: bg: job " << id << " already in background" << std::endl;
        return 0;
    }
    state.jobs.run_in_background(id);
    return 0;
}

// Name -> function table. Adding a built-in = one new line here.
const std::unordered_map<std::string, BuiltinFn>& builtin_table() {
    static const std::unordered_map<std::string, BuiltinFn> table = {
        {"cd", builtin_cd},         {"pwd", builtin_pwd},
        {"echo", builtin_echo},     {"exit", builtin_exit},
        {"clear", builtin_clear},   {"help", builtin_help},
        {"history", builtin_history}, {"export", builtin_export},
        {"jobs", builtin_jobs},   {"fg", builtin_fg},
        {"bg", builtin_bg},
    };
    return table;
}

}  // namespace

bool is_builtin(const std::string& name) {
    return builtin_table().count(name) > 0;
}

int run_builtin(ShellState& state, const Args& args) {
    return builtin_table().at(args[0])(state, args);
}
