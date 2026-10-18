// NexusShell - input.cpp
// Phase 9: a small line editor.
//
// Canonical mode (the default) makes the kernel collect a whole line and handle
// backspace for us, but then we only see the line after Enter: no arrow keys.
// So while reading we clear two termios flags:
//   ICANON  deliver every byte immediately instead of line by line
//   ECHO    the kernel stops printing typed characters; WE do it
// ISIG stays on, so Ctrl+C still raises SIGINT and Ctrl+Z is still ignored.
//
// Arrow keys arrive as escape sequences: Up = ESC [ A, Delete = ESC [ 3 ~ ...

#include "input.h"

#include <cerrno>
#include <csignal>
#include <iostream>

#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include "signals.h"

namespace {

enum Key { kEof = -1, kInterrupted = -2, kTimeout = -3 };

void put(const std::string& text) {
    // write() directly: the editor owns the terminal while it reads a line.
    size_t done = 0;
    while (done < text.size()) {
        ssize_t n = write(STDOUT_FILENO, text.data() + done, text.size() - done);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        done += static_cast<size_t>(n);
    }
}

// Wait for input on stdin. Returns 1 if a byte is ready, 0 on timeout,
// -1 if Ctrl+C arrived. timeout_ms < 0 waits forever.
//
// The race to avoid: if SIGINT arrives AFTER we check the flag but BEFORE we
// sleep, the wakeup is lost. So SIGINT is blocked while we check, and ppoll()
// unblocks it and sleeps in one atomic step (the same idea as sigsuspend).
int wait_for_input(int timeout_ms) {
    sigset_t block, old;
    sigemptyset(&block);
    sigaddset(&block, SIGINT);
    sigprocmask(SIG_BLOCK, &block, &old);

    int result = 0;
    for (;;) {
        if (take_sigint()) {
            result = -1;
            break;
        }
        pollfd pfd = {STDIN_FILENO, POLLIN, 0};
        timespec limit = {timeout_ms / 1000, (timeout_ms % 1000) * 1000000L};
        int r = ppoll(&pfd, 1, timeout_ms < 0 ? nullptr : &limit, &old);
        if (r > 0) {
            result = 1;
            break;
        }
        if (r == 0) {
            result = 0;
            break;
        }
        // r < 0: a signal interrupted us. SIGCHLD just means "look again";
        // SIGINT is noticed by take_sigint() at the top of the loop.
    }
    sigprocmask(SIG_SETMASK, &old, nullptr);
    return result;
}

// Read one byte, or return a negative Key value.
int read_byte(int timeout_ms) {
    for (;;) {
        int ready = wait_for_input(timeout_ms);
        if (ready < 0) {
            return kInterrupted;
        }
        if (ready == 0) {
            return kTimeout;
        }
        unsigned char c = 0;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n == 1) {
            return c;
        }
        if (n == 0) {
            return kEof;
        }
        if (errno == EINTR) {
            if (take_sigint()) {
                return kInterrupted;
            }
            continue;
        }
        return kEof;
    }
}

// Raw mode for the lifetime of the object; the old settings come back in the
// destructor, whichever way read_line() returns.
class RawMode {
public:
    RawMode() {
        if (tcgetattr(STDIN_FILENO, &saved_) != 0) {
            return;
        }
        termios raw = saved_;
        raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO | IEXTEN);
        raw.c_cc[VMIN] = 1;   // read() returns after 1 byte...
        raw.c_cc[VTIME] = 0;  // ...with no timer
        active_ = tcsetattr(STDIN_FILENO, TCSADRAIN, &raw) == 0;
    }
    ~RawMode() {
        if (active_) {
            tcsetattr(STDIN_FILENO, TCSADRAIN, &saved_);
        }
    }
    bool active() const { return active_; }
    RawMode(const RawMode&) = delete;
    RawMode& operator=(const RawMode&) = delete;

private:
    termios saved_{};
    bool active_ = false;
};

// The line being edited and the cursor inside it.
class Editor {
public:
    Editor(const std::string& prompt, const std::vector<std::string>& history)
        : prompt_(prompt), history_(history), history_pos_(history.size()) {}

    ReadResult run(std::string& line) {
        std::cout.flush();
        put(prompt_);

        for (;;) {
            int c = read_byte(-1);
            if (c == kInterrupted) {
                put("^C\r\n");
                return ReadResult::Interrupted;
            }
            if (c == kEof) {
                put("\r\n");
                return ReadResult::Eof;
            }

            switch (c) {
                case '\r':
                case '\n':
                    put("\r\n");
                    line = buffer_;
                    return ReadResult::Line;
                case 4:  // Ctrl+D: end of input on an empty line, else delete
                    if (buffer_.empty()) {
                        put("\r\n");
                        return ReadResult::Eof;
                    }
                    delete_at_cursor();
                    break;
                case 127:  // Backspace
                case 8:
                    backspace();
                    break;
                case 1: move_to(0); break;                  // Ctrl+A
                case 5: move_to(buffer_.size()); break;     // Ctrl+E
                case 2: move_to(cursor_ > 0 ? cursor_ - 1 : 0); break;  // Ctrl+B
                case 6: move_to(cursor_ < buffer_.size() ? cursor_ + 1 : cursor_); break;  // Ctrl+F
                case 11:  // Ctrl+K: delete to end of line
                    buffer_.erase(cursor_);
                    redraw();
                    break;
                case 21:  // Ctrl+U: delete to start of line
                    buffer_.erase(0, cursor_);
                    cursor_ = 0;
                    redraw();
                    break;
                case 23:  // Ctrl+W: delete the word before the cursor
                    delete_word();
                    break;
                case 12:  // Ctrl+L: clear the screen, keep the line
                    put("\033[H\033[2J");
                    redraw();
                    break;
                case 16: history_up(); break;    // Ctrl+P
                case 14: history_down(); break;  // Ctrl+N
                case 27: {
                    int result = escape_sequence();
                    if (result == kInterrupted) {
                        put("^C\r\n");
                        return ReadResult::Interrupted;
                    }
                    break;
                }
                default:
                    if (c >= 32) {  // printable (bytes >= 128 are passed through)
                        insert(static_cast<char>(c));
                    }
                    // other control characters (Tab, Ctrl+X, ...) are ignored
                    break;
            }
        }
    }

private:
    // Decode what follows an ESC byte. Returns kInterrupted if Ctrl+C arrives.
    int escape_sequence() {
        int c = read_byte(50);  // a lone ESC key has nothing after it
        if (c == kInterrupted) {
            return kInterrupted;
        }
        if (c != '[' && c != 'O') {
            return 0;  // lone ESC, or Alt+key: ignore
        }
        bool application = (c == 'O');  // some terminals send ESC O A for Up

        std::string params;
        int final_byte = 0;
        for (;;) {
            c = read_byte(50);
            if (c == kInterrupted) {
                return kInterrupted;
            }
            if (c < 0) {
                return 0;
            }
            if (c >= '@' && c <= '~') {  // the byte that ends the sequence
                final_byte = c;
                break;
            }
            params += static_cast<char>(c);
            if (application) {
                break;
            }
        }
        if (application) {
            final_byte = params.empty() ? 0 : params[0];
            params.clear();
        }

        switch (final_byte) {
            case 'A': history_up(); break;
            case 'B': history_down(); break;
            case 'C': move_to(cursor_ < buffer_.size() ? cursor_ + 1 : cursor_); break;
            case 'D': move_to(cursor_ > 0 ? cursor_ - 1 : 0); break;
            case 'H': move_to(0); break;
            case 'F': move_to(buffer_.size()); break;
            case '~':
                if (params == "1" || params == "7") {
                    move_to(0);                // Home
                } else if (params == "4" || params == "8") {
                    move_to(buffer_.size());   // End
                } else if (params == "3") {
                    delete_at_cursor();        // Delete
                }
                break;
            default: break;
        }
        return 0;
    }

    // Repaint the whole line: back to column 0, prompt, text, erase the rest,
    // then walk the cursor back to where it belongs.
    void redraw() {
        put("\r" + prompt_ + buffer_ + "\033[K");
        size_t back = buffer_.size() - cursor_;
        if (back > 0) {
            put("\033[" + std::to_string(back) + "D");
        }
    }

    void insert(char c) {
        buffer_.insert(cursor_, 1, c);
        ++cursor_;
        if (cursor_ == buffer_.size()) {
            put(std::string(1, c));  // typing at the end: just echo the byte
        } else {
            redraw();
        }
    }

    void backspace() {
        if (cursor_ == 0) {
            return;
        }
        buffer_.erase(cursor_ - 1, 1);
        --cursor_;
        redraw();
    }

    void delete_at_cursor() {
        if (cursor_ >= buffer_.size()) {
            return;
        }
        buffer_.erase(cursor_, 1);
        redraw();
    }

    void delete_word() {
        size_t start = cursor_;
        while (start > 0 && buffer_[start - 1] == ' ') {
            --start;
        }
        while (start > 0 && buffer_[start - 1] != ' ') {
            --start;
        }
        buffer_.erase(start, cursor_ - start);
        cursor_ = start;
        redraw();
    }

    void move_to(size_t position) {
        cursor_ = position;
        redraw();
    }

    void show(const std::string& text) {
        buffer_ = text;
        cursor_ = buffer_.size();
        redraw();
    }

    void history_up() {
        if (history_pos_ == 0) {
            return;
        }
        if (history_pos_ == history_.size()) {
            draft_ = buffer_;  // keep what was being typed
        }
        --history_pos_;
        show(history_[history_pos_]);
    }

    void history_down() {
        if (history_pos_ >= history_.size()) {
            return;
        }
        ++history_pos_;
        show(history_pos_ == history_.size() ? draft_ : history_[history_pos_]);
    }

    std::string prompt_;
    const std::vector<std::string>& history_;
    size_t history_pos_;  // history_.size() means "the line being typed"
    std::string draft_;
    std::string buffer_;
    size_t cursor_ = 0;
};

}  // namespace

ReadResult read_line(const std::string& prompt, const std::vector<std::string>& history,
                     std::string& line) {
    if (isatty(STDIN_FILENO)) {
        RawMode raw;
        if (raw.active()) {
            Editor editor(prompt, history);
            return editor.run(line);
        }
    }

    // Not a terminal: no editing, no history keys.
    std::cout << prompt;
    if (!std::getline(std::cin, line)) {
        std::cout << std::endl;
        return ReadResult::Eof;
    }
    return ReadResult::Line;
}
