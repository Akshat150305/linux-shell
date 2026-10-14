// NexusShell - redirect.h
// File redirection with open() + dup2(): rewire a process's file descriptors
// (0 = stdin, 1 = stdout, 2 = stderr) to point at files.

#ifndef NEXUSSHELL_REDIRECT_H
#define NEXUSSHELL_REDIRECT_H

#include <string>
#include <vector>

struct Redirection {
    enum class Kind {
        Read,    // <   open for reading
        Write,   // >   open for writing, create, truncate   (also 2>)
        Append,  // >>  open for writing, create, append
    };

    int fd;            // descriptor to rewire: 0, 1 or 2
    Kind kind;
    std::string path;  // the file to open
};

// Flush every C++/C output buffer. Must happen before descriptors are swapped
// (and before they are restored), or buffered text lands in the wrong file.
void flush_output();

// Apply the redirections in order, permanently, to the CURRENT process.
// Used in a forked child just before execvp (the parent is untouched).
// Returns false and sets `error` ("path: reason") on the first failure.
bool apply_redirections(const std::vector<Redirection>& redirections,
                        std::string& error);

// For built-ins, which run inside the shell itself (no fork): applies
// redirections but remembers the original descriptors and puts them back on
// restore() or when the scope ends.
class RedirectScope {
public:
    RedirectScope() = default;
    ~RedirectScope();
    RedirectScope(const RedirectScope&) = delete;
    RedirectScope& operator=(const RedirectScope&) = delete;

    bool apply(const std::vector<Redirection>& redirections, std::string& error);
    void restore();

private:
    bool touched_[3] = {false, false, false};
    int saved_[3] = {-1, -1, -1};  // duplicate of the original descriptor
};

#endif  // NEXUSSHELL_REDIRECT_H
