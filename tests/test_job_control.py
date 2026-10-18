#!/usr/bin/env python3
"""Phase 8 end-to-end tests: Ctrl+C, Ctrl+Z, fg, bg, SIGCHLD reaping.

Job control needs a real controlling terminal, so each test runs ./nexusshell
inside a pseudo-terminal (pty) and types keys into it like a person would:
"\\x03" is Ctrl+C, "\\x1a" is Ctrl+Z.

Run:  python3 tests/test_job_control.py      (after `make`)
"""

import os
import pty
import re
import select
import signal
import subprocess
import sys
import time

SHELL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "nexusshell")
PROMPT = "nexus> "
CTRL_C, CTRL_Z, CTRL_D = "\x03", "\x1a", "\x04"


class Session:
    """One shell running on a pty."""

    def __init__(self):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.execv(SHELL, [SHELL])
        self.buf = ""

    def send(self, text):
        os.write(self.fd, text.encode())

    def expect(self, text, timeout=5.0):
        """Wait until `text` appears; return everything before it."""
        end = time.time() + timeout
        while True:
            i = self.buf.find(text)
            if i >= 0:
                before = self.buf[:i]
                self.buf = self.buf[i + len(text):]
                return before
            left = end - time.time()
            if left <= 0:
                raise AssertionError("timeout waiting for %r; got %r" % (text, self.buf))
            ready, _, _ = select.select([self.fd], [], [], left)
            if ready:
                try:
                    data = os.read(self.fd, 4096)
                except OSError:
                    data = b""
                if not data:
                    raise AssertionError("shell closed; waiting for %r, got %r" % (text, self.buf))
                self.buf += data.decode(errors="replace")

    def run(self, line, timeout=5.0):
        """Type a line, wait for the next prompt, return the output."""
        self.send(line + "\n")
        out = self.expect(PROMPT, timeout)
        return out.replace("\r", "")

    def alive(self):
        pid, _ = os.waitpid(self.pid, os.WNOHANG)
        return pid == 0

    def children(self):
        """(pid, stat, command) of the shell's children."""
        out = subprocess.run(["ps", "-o", "pid=,stat=,comm=", "--ppid", str(self.pid)],
                             capture_output=True, text=True).stdout
        return [tuple(l.split(None, 2)) for l in out.splitlines() if l.strip()]

    def close(self):
        # Kill the shell's children too, or stopped jobs would outlive the test.
        subprocess.run(["pkill", "-KILL", "-P", str(self.pid)])
        try:
            os.kill(self.pid, signal.SIGKILL)
            os.waitpid(self.pid, 0)
        except OSError:
            pass
        os.close(self.fd)


def start():
    s = Session()
    s.expect(PROMPT)
    return s


def status_of(s):
    out = s.run("echo STATUS=$?")
    return int(re.search(r"STATUS=(\d+)", out.split("\n")[-2] if "\n" in out else out).group(1))


# ---------------------------------------------------------------- tests ----

def test_ctrl_c_kills_foreground_not_shell():
    s = start()
    s.send("sleep 30\n")
    time.sleep(0.3)
    s.send(CTRL_C)
    s.expect(PROMPT)
    assert s.alive(), "shell died on Ctrl+C"
    assert status_of(s) == 130, "status after Ctrl+C should be 130"
    assert not s.children(), "sleep should be gone"
    s.close()


def test_ctrl_c_at_empty_prompt():
    s = start()
    s.send(CTRL_C)
    s.expect(PROMPT)
    assert s.alive()
    assert "hi" in s.run("echo hi")
    s.close()


def test_ctrl_c_discards_partial_line():
    s = start()
    s.send("echo never")
    s.send(CTRL_C)
    s.expect(PROMPT)
    out = s.run("echo ok")
    assert "never" not in out.replace("echo ok", ""), out
    s.close()


def test_ctrl_z_stops_and_jobs_lists():
    s = start()
    s.send("sleep 30\n")
    time.sleep(0.3)
    s.send(CTRL_Z)
    out = s.expect(PROMPT).replace("\r", "")
    assert "[1]+  Stopped" in out and "sleep 30" in out, out
    assert s.alive()
    assert status_of(s) == 148
    assert "Stopped" in s.run("jobs")
    s.close()


def test_ctrl_z_at_prompt_is_ignored():
    s = start()
    s.send(CTRL_Z)
    time.sleep(0.2)
    assert s.alive()
    assert "hi" in s.run("echo hi")
    s.close()


def test_bg_then_fg_then_ctrl_c():
    s = start()
    s.send("sleep 30\n")
    time.sleep(0.3)
    s.send(CTRL_Z)
    s.expect(PROMPT)
    out = s.run("bg")
    assert "[1]+ sleep 30 &" in out, out
    assert "Running" in s.run("jobs")
    time.sleep(0.2)
    s.send("fg\n")
    time.sleep(0.4)
    s.send(CTRL_C)
    out = s.expect(PROMPT)
    assert "sleep 30" in out  # fg prints the command it resumes
    assert status_of(s) == 130
    assert "[1]" not in s.run("jobs")
    s.close()


def test_fg_of_stopped_job_finishes_normally():
    s = start()
    s.send("sh -c 'sleep 1; exit 7'\n")
    time.sleep(0.3)
    s.send(CTRL_Z)
    s.expect(PROMPT)
    s.send("fg\n")
    s.expect(PROMPT, timeout=5)
    assert status_of(s) == 7
    s.close()


def test_ctrl_c_hits_whole_pipeline():
    s = start()
    s.send("sleep 30 | sleep 30 | sleep 30\n")
    time.sleep(0.4)
    assert len(s.children()) == 3
    s.send(CTRL_C)
    s.expect(PROMPT)
    assert not s.children(), s.children()
    s.close()


def test_ctrl_z_stops_whole_pipeline():
    s = start()
    s.send("sleep 30 | sleep 30\n")
    time.sleep(0.4)
    s.send(CTRL_Z)
    s.expect(PROMPT)
    assert all("T" in st for _, st, _ in s.children()), s.children()
    s.send("fg\n")  # resume the whole group, then Ctrl+C kills all of it
    time.sleep(0.4)
    s.send(CTRL_C)
    s.expect(PROMPT)
    assert not s.children(), s.children()
    s.close()


def test_ctrl_c_spares_background_jobs():
    s = start()
    s.run("sleep 30 &")
    s.send("sleep 30\n")
    time.sleep(0.3)
    s.send(CTRL_C)
    s.expect(PROMPT)
    assert "Running" in s.run("jobs")
    assert len(s.children()) == 1
    s.close()


def test_background_job_is_reaped_without_a_prompt():
    # SIGCHLD reaps the child the moment it exits, not at the next prompt.
    s = start()
    s.run("sleep 0.3 &")
    time.sleep(0.8)  # no input from us in between
    assert not s.children(), "zombie left behind: %r" % s.children()
    out = s.run("")  # a blank line: the Done notice comes with the next prompt
    s.close()


def test_done_notice_appears():
    s = start()
    s.run("sleep 0.2 &")
    time.sleep(0.6)
    out = s.run("echo x")
    assert "Done" in out and "sleep 0.2" in out, out
    s.close()


def test_many_background_jobs_leave_no_zombies():
    s = start()
    for _ in range(25):
        s.run("true &")
    time.sleep(0.5)
    assert not s.children(), s.children()
    s.close()


def test_killed_by_signal_message():
    s = start()
    out = s.run("sh -c 'kill -9 $$'")
    assert "Killed" in out, out
    assert status_of(s) == 137
    s.close()


def test_exit_warns_about_stopped_jobs_then_hangs_them_up():
    s = start()
    s.send("sleep 30\n")
    time.sleep(0.3)
    s.send(CTRL_Z)
    s.expect(PROMPT)
    job_pid = s.children()[0][0]
    out = s.run("exit")
    assert "There are stopped jobs." in out, out
    assert s.alive()
    s.send("exit\n")
    time.sleep(0.5)
    assert not s.alive()
    time.sleep(0.3)
    assert not os.path.exists("/proc/%s" % job_pid) or "Z" in open("/proc/%s/stat" % job_pid).read().split(")")[1][:3], \
        "stopped job survived exit"
    os.close(s.fd)


def test_ctrl_d_warns_then_exits():
    s = start()
    s.send("sleep 30\n")
    time.sleep(0.3)
    s.send(CTRL_Z)
    s.expect(PROMPT)
    s.send(CTRL_D)
    out = s.expect(PROMPT)
    assert "There are stopped jobs." in out, out
    s.send(CTRL_D)
    time.sleep(0.5)
    assert not s.alive()
    os.close(s.fd)


def test_stty_change_survives_a_foreground_job():
    s = start()
    s.run("stty -echo")
    s.send("sleep 30\n")
    time.sleep(0.3)
    s.send(CTRL_Z)
    s.expect(PROMPT)
    s.send("fg\n")
    time.sleep(0.3)
    s.send(CTRL_C)
    s.expect(PROMPT)
    s.send("stty -a | grep -o -- '-ech[o] '\n")
    out = s.expect(PROMPT)
    assert "-echo" in out, out  # [o] keeps the typed command from matching itself
    s.close()


def test_fg_bg_errors():
    s = start()
    assert "no such job" in s.run("fg")
    assert "no such job" in s.run("bg %5")
    s.run("sleep 30 &")
    assert "already in background" in s.run("bg")
    assert "no such job" in s.run("fg %9")
    s.close()


def test_job_control_with_redirected_builtin_stdin():
    s = start()
    s.send("sleep 30\n")
    time.sleep(0.3)
    s.send(CTRL_Z)
    s.expect(PROMPT)
    out = s.run("jobs < /dev/null")  # stdin of the built-in is not the terminal
    assert "Stopped" in out, out
    s.send("fg < /dev/null\n")  # must still hand over the terminal
    time.sleep(0.4)
    s.send(CTRL_C)
    s.expect(PROMPT)
    assert status_of(s) == 130
    s.close()


def test_children_get_default_signals():
    # SIG_IGN would survive exec: the child must NOT inherit the shell's ignores.
    s = start()
    out = s.run("sh -c 'grep -E \"^Sig(Ign|Cgt)\" /proc/self/status'")
    ign = int(re.search(r"SigIgn:\s*([0-9a-f]+)", out).group(1), 16)
    # The signals the shell ignores for itself: INT is handled, QUIT/TSTP/TTIN/TTOU ignored.
    touched = [signal.SIGINT, signal.SIGQUIT, signal.SIGTSTP, signal.SIGTTIN, signal.SIGTTOU]
    inherited = [int(n) for n in touched if ign & (1 << (int(n) - 1))]
    assert not inherited, "child inherited ignored signals: %r" % inherited
    s.close()


def main():
    tests = [(n, f) for n, f in sorted(globals().items()) if n.startswith("test_")]
    failed = 0
    for name, fn in tests:
        try:
            fn()
            print("ok    " + name)
        except Exception as e:  # noqa: BLE001
            failed += 1
            print("FAIL  %s: %s" % (name, e))
    print("%d/%d job-control tests passed" % (len(tests) - failed, len(tests)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
