// NexusShell - redirect.cpp
// Phase 5: I/O redirection with open(), dup2() and close().

#include "redirect.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>

#include <fcntl.h>
#include <unistd.h>

namespace {

int open_flags(Redirection::Kind kind) {
    switch (kind) {
        case Redirection::Kind::Read:
            return O_RDONLY;
        case Redirection::Kind::Write:
            return O_WRONLY | O_CREAT | O_TRUNC;
        case Redirection::Kind::Append:
            return O_WRONLY | O_CREAT | O_APPEND;
    }
    return O_RDONLY;
}

// Open one redirection target and make `redirection.fd` point at it.
bool apply_one(const Redirection& r, std::string& error) {
    // O_CLOEXEC: if anything goes wrong, the spare descriptor never leaks into
    // a program we exec. dup2() clears the flag on the copy it makes.
    // 0666 is filtered by the umask, giving the usual 0644 for new files.
    int fd = open(r.path.c_str(), open_flags(r.kind) | O_CLOEXEC, 0666);
    if (fd < 0) {
        error = r.path + ": " + std::strerror(errno);
        return false;
    }

    if (fd == r.fd) {
        // open() already handed us the descriptor we wanted (only possible if
        // that number was closed). Just make sure it survives exec.
        fcntl(fd, F_SETFD, 0);
        return true;
    }

    if (dup2(fd, r.fd) < 0) {
        int saved_errno = errno;
        close(fd);
        error = r.path + ": " + std::strerror(saved_errno);
        return false;
    }
    close(fd);  // the spare copy is no longer needed
    return true;
}

}  // namespace

void flush_output() {
    std::cout.flush();
    std::cerr.flush();
    std::fflush(nullptr);  // all C stdio streams
}

bool apply_redirections(const std::vector<Redirection>& redirections,
                        std::string& error) {
    for (const Redirection& r : redirections) {
        if (!apply_one(r, error)) {
            return false;
        }
    }
    return true;
}

bool RedirectScope::apply(const std::vector<Redirection>& redirections,
                          std::string& error) {
    flush_output();  // anything already printed belongs to the OLD destination

    for (const Redirection& r : redirections) {
        if (!touched_[r.fd]) {
            // Keep a copy of the original descriptor, above 10 so it cannot
            // collide with anything a redirection opens.
            saved_[r.fd] = fcntl(r.fd, F_DUPFD_CLOEXEC, 10);  // -1 if it was closed
            touched_[r.fd] = true;
        }
        if (!apply_one(r, error)) {
            return false;  // the destructor restores whatever was changed
        }
    }
    return true;
}

void RedirectScope::restore() {
    flush_output();  // built-in output must reach the redirected file first

    for (int fd = 0; fd < 3; ++fd) {
        if (!touched_[fd]) {
            continue;
        }
        if (saved_[fd] >= 0) {
            dup2(saved_[fd], fd);
            close(saved_[fd]);
        } else {
            close(fd);  // it was not open before we started
        }
        touched_[fd] = false;
        saved_[fd] = -1;
    }
}

RedirectScope::~RedirectScope() {
    restore();
}
