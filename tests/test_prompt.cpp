// Unit tests for prompt.cpp: formatting is a pure function of PromptInfo.

#include <iostream>

#include "prompt.h"

static int g_total = 0;
static int g_failed = 0;

#define CHECK_EQ(actual, expected)                                                    \
    do {                                                                              \
        ++g_total;                                                                    \
        std::string a_ = (actual);                                                    \
        std::string e_ = (expected);                                                  \
        if (a_ != e_) {                                                               \
            ++g_failed;                                                               \
            std::cerr << "FAIL line " << __LINE__ << ": got [" << a_ << "] want ["    \
                      << e_ << "]" << std::endl;                                      \
        }                                                                             \
    } while (0)

static PromptInfo info(const std::string& cwd, int status = 0, bool root = false,
                       bool color = false) {
    PromptInfo p;
    p.user = "akshat";
    p.host = "nexus";
    p.cwd = cwd;
    p.home = "/home/akshat";
    p.status = status;
    p.root = root;
    p.color = color;
    return p;
}

int main() {
    // --- abbreviate_home ---
    CHECK_EQ(abbreviate_home("/home/akshat", "/home/akshat"), "~");
    CHECK_EQ(abbreviate_home("/home/akshat/projects/linux-shell", "/home/akshat"),
             "~/projects/linux-shell");
    CHECK_EQ(abbreviate_home("/home/akshatx", "/home/akshat"), "/home/akshatx");  // not a child
    CHECK_EQ(abbreviate_home("/home/aks", "/home/akshat"), "/home/aks");
    CHECK_EQ(abbreviate_home("/tmp", "/home/akshat"), "/tmp");
    CHECK_EQ(abbreviate_home("/", "/home/akshat"), "/");
    CHECK_EQ(abbreviate_home("/tmp", ""), "/tmp");                  // no $HOME
    CHECK_EQ(abbreviate_home("/usr", "/"), "/usr");                 // HOME=/ would make everything ~
    CHECK_EQ(abbreviate_home("/home/akshat/", "/home/akshat"), "~/");  // trailing slash kept

    // --- plain prompt (the example from the project plan) ---
    CHECK_EQ(format_prompt(info("/home/akshat/projects/linux-shell")),
             "akshat@nexus:~/projects/linux-shell $ ");
    CHECK_EQ(format_prompt(info("/home/akshat")), "akshat@nexus:~ $ ");
    CHECK_EQ(format_prompt(info("/")), "akshat@nexus:/ $ ");
    CHECK_EQ(format_prompt(info("/var/log", 0, true)), "akshat@nexus:/var/log # ");  // root

    // --- exit status indicator ---
    CHECK_EQ(format_prompt(info("/tmp", 1)), "[1] akshat@nexus:/tmp $ ");
    CHECK_EQ(format_prompt(info("/tmp", 127)), "[127] akshat@nexus:/tmp $ ");
    CHECK_EQ(format_prompt(info("/tmp", 130)), "[130] akshat@nexus:/tmp $ ");
    CHECK_EQ(format_prompt(info("/tmp", 0)), "akshat@nexus:/tmp $ ");  // success: no indicator

    // --- colour ---
    CHECK_EQ(format_prompt(info("/tmp", 0, false, true)),
             "\033[1;32makshat@nexus\033[0m:\033[1;34m/tmp\033[0m $ ");
    CHECK_EQ(format_prompt(info("/tmp", 2, false, true)),
             "\033[1;31m[2]\033[0m \033[1;32makshat@nexus\033[0m:\033[1;34m/tmp\033[0m $ ");

    // --- banner ---
    CHECK_EQ(welcome_banner(false),
             "NexusShell 1.0 - a Unix shell written from scratch in C++\n"
             "Type 'help' for the built-in commands, 'exit' to leave.\n");
    ++g_total;
    if (welcome_banner(true).find("\033[") == std::string::npos) {
        ++g_failed;
        std::cerr << "FAIL: coloured banner has no escape codes" << std::endl;
    }

    std::cout << (g_total - g_failed) << "/" << g_total << " prompt tests passed" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
