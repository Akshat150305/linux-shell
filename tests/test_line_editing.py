#!/usr/bin/env python3
"""Phase 9 end-to-end tests: line editing, history recall, persistence, `!` expansion.

Like test_job_control.py, every test types real key sequences into ./nexusshell
running on a pseudo-terminal. Each session uses a throwaway history file
(NEXUSSHELL_HISTFILE), so your real ~/.nexusshell_history is never touched.

Run:  python3 tests/test_line_editing.py      (after `make`)
"""

import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_job_control import CTRL_C, CTRL_D, PROMPT, SHELL, Session, start  # noqa: E402

UP, DOWN, RIGHT, LEFT = "\x1b[A", "\x1b[B", "\x1b[C", "\x1b[D"
HOME, END, DELETE = "\x1b[H", "\x1b[F", "\x1b[3~"
CTRL_A, CTRL_E, CTRL_K, CTRL_U, CTRL_W, CTRL_L = "\x01", "\x05", "\x0b", "\x15", "\x17", "\x0c"


def output_lines(out):
    """Lines of output, without the echoed command line (which has the prompt)."""
    return [l.strip("\r") for l in out.split("\n")]


def keys(s, text):
    """Type `text` (keys included) and press Enter; return the output."""
    s.send(text + "\n")
    return output_lines(s.expect(PROMPT))


# ------------------------------------------------------------ editing ----

def test_typing_and_enter():
    s = start()
    assert "hello" in keys(s, "echo hello")
    s.close()


def test_backspace():
    s = start()
    assert "ab c" in keys(s, "echo abx\x7f c")
    s.close()


def test_left_arrow_inserts_in_the_middle():
    s = start()
    assert "abc" in keys(s, "echo ac" + LEFT + "b")
    s.close()


def test_home_end_and_ctrl_a_e():
    s = start()
    assert "hi" in keys(s, "cho hi" + CTRL_A + "e")
    assert "hi!" in keys(s, "echo hi" + HOME + END + "!")
    assert "hi?" in keys(s, "echo hi" + CTRL_A + CTRL_E + "?")
    s.close()


def test_delete_key_and_ctrl_d_inside_a_line():
    s = start()
    assert "hi" in keys(s, "echo Xhi" + HOME + RIGHT * 5 + DELETE)
    assert "hi" in keys(s, "echo hxi" + LEFT * 2 + CTRL_D)
    assert s.alive()
    s.close()


def test_kill_keys():
    s = start()
    assert "good" in keys(s, "echo bad" + CTRL_U + "echo good")
    assert "one" in keys(s, "echo one two" + CTRL_W)            # deletes "two"
    assert "one" in keys(s, "echo one XXXX" + CTRL_A + RIGHT * 9 + CTRL_K)
    s.close()


def test_ctrl_l_keeps_the_line():
    s = start()
    s.send("echo keep" + CTRL_L)
    time.sleep(0.2)
    s.send("\n")
    out = output_lines(s.expect(PROMPT))
    assert "keep" in out
    s.close()


def test_lone_escape_does_not_hang():
    s = start()
    s.send("\x1b")
    time.sleep(0.2)
    assert "ok" in keys(s, "echo ok")
    s.close()


def test_unknown_keys_are_ignored():
    s = start()
    assert "ok" in keys(s, "echo \t\x18ok")   # Tab and Ctrl+X do nothing
    s.close()


def test_ctrl_d_on_empty_line_exits():
    s = start()
    s.send(CTRL_D)
    time.sleep(0.4)
    assert not s.alive()
    os.close(s.fd)


def test_ctrl_c_discards_line_and_next_up_still_works():
    s = start()
    keys(s, "echo first")
    s.send("echo half" + CTRL_C)
    s.expect(PROMPT)
    assert "first" in keys(s, UP)
    s.close()


def test_ctrl_c_is_never_lost():
    # Stress the SIGINT race: hit Ctrl+C the instant each prompt appears.
    s = start()
    for _ in range(60):
        s.send(CTRL_C)
        s.expect(PROMPT, timeout=3)
    assert s.alive()
    assert "fine" in keys(s, "echo fine")
    s.close()


def test_terminal_is_cooked_while_commands_run():
    s = start()
    # The editor's raw mode must be gone: icanon and echo are ON for children.
    assert "0" in keys(s, "stty -a | grep -c -- '-icano[n]'")
    assert "1" in keys(s, "stty -a | grep -c -- ' echo '")
    s.close()


# ------------------------------------------------------------ history ----

def test_up_recalls_previous_commands():
    s = start()
    keys(s, "echo one")
    keys(s, "echo two")
    assert "two" in keys(s, UP)
    assert "one" in keys(s, UP + UP)
    s.close()


def test_down_returns_to_the_unfinished_line():
    s = start()
    keys(s, "echo one")
    assert "xyz" in keys(s, "echo xy" + UP + DOWN + "z")
    s.close()


def test_up_and_down_with_nothing_to_recall():
    s = start()
    assert "ok" in keys(s, UP + DOWN + UP + "echo ok")
    s.close()


def test_recalled_line_can_be_edited():
    s = start()
    keys(s, "echo abc")
    assert "abXc" in keys(s, UP + LEFT + "X")
    s.close()


def test_history_numbers_and_count():
    s = start()
    keys(s, "echo a")
    keys(s, "echo b")
    lines = keys(s, "history")
    assert any(l.strip().startswith("1") and l.endswith("echo a") for l in lines), lines
    assert any(l.strip().startswith("3") and l.endswith("history") for l in lines), lines
    lines = keys(s, "history 2")
    body = [l for l in lines if l.strip() and l.strip()[0].isdigit()]
    assert len(body) == 2, body
    assert "numeric argument required" in " ".join(keys(s, "history abc"))
    s.close()


def test_blank_and_duplicate_lines_are_not_recorded():
    s = start()
    keys(s, "echo x")
    keys(s, "echo x")
    keys(s, "   ")
    keys(s, "")
    lines = [l for l in keys(s, "history") if l.strip() and l.strip()[0].isdigit()]
    assert len(lines) == 2, lines   # "echo x" once, then "history"
    s.close()


def test_bang_expansion():
    s = start()
    keys(s, "echo a")
    keys(s, "echo b")
    out = keys(s, "!!")
    assert "echo b" in out and "b" in out        # the expanded line is shown, then runs
    assert "a" in keys(s, "!1")
    assert "a" in keys(s, "!-3")                 # 3rd from the end
    assert "event not found" in " ".join(keys(s, "!zzz"))
    assert "event not found" in " ".join(keys(s, "!99"))
    assert "x" in keys(s, "echo x | cat")
    out = keys(s, "!ec")                         # newest command starting with "ec"
    assert "x" in out
    s.close()


def test_bang_only_at_start_of_line():
    s = start()
    keys(s, "echo a")
    assert "!!" in keys(s, "echo '!!'")
    assert "!!" in keys(s, "echo !!")
    s.close()


# -------------------------------------------------------- persistence ----

def test_history_is_saved_and_reloaded():
    path = tempfile.mktemp(prefix="nexus_hist_")
    s = Session(path)
    s.expect(PROMPT)
    keys(s, "echo persist_me")
    keys(s, "echo and_me")
    assert os.path.exists(path)
    assert oct(os.stat(path).st_mode & 0o777) == "0o600"      # private: may contain secrets
    assert open(path).read() == "echo persist_me\necho and_me\n"
    s.close(keep_history=True)

    s = Session(path)                                         # a brand-new shell
    s.expect(PROMPT)
    assert "and_me" in keys(s, UP)
    assert "persist_me" in keys(s, UP + UP)
    lines = keys(s, "history")
    assert any(l.endswith("echo persist_me") for l in lines), lines
    s.close()


def test_history_clear_empties_memory_and_file():
    path = tempfile.mktemp(prefix="nexus_hist_")
    s = Session(path)
    s.expect(PROMPT)
    keys(s, "echo secret")
    keys(s, "history -c")
    assert open(path).read() == ""
    lines = [l for l in keys(s, "history") if l.strip() and l.strip()[0].isdigit()]
    assert len(lines) == 1 and lines[0].endswith("history"), lines
    s.close(keep_history=True)
    s = Session(path)
    s.expect(PROMPT)
    # Only the `history` that was typed AFTER the clear survives; "secret" is gone.
    assert "secret" not in " ".join(keys(s, "history"))
    s.close()


def test_long_history_file_is_trimmed_on_start():
    path = tempfile.mktemp(prefix="nexus_hist_")
    with open(path, "w") as f:
        for i in range(1100):
            f.write("echo line%d\n" % i)
    s = Session(path)
    s.expect(PROMPT)
    assert len(open(path).read().splitlines()) == 1000
    assert "line1099" in keys(s, UP)
    s.close()


def test_scripts_never_touch_the_history_file():
    path = tempfile.mktemp(prefix="nexus_hist_")
    env = dict(os.environ, NEXUSSHELL_HISTFILE=path)
    out = subprocess.run([SHELL], input="echo hi\nhistory\n", capture_output=True,
                         text=True, env=env).stdout
    assert not os.path.exists(path), "a non-interactive shell wrote the history file"
    assert "echo hi" in out          # in-memory history still works in scripts


def test_missing_history_directory_is_not_fatal():
    s = Session("/nonexistent_dir_xyz/hist")
    s.expect(PROMPT)
    assert "ok" in keys(s, "echo ok")
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
    print("%d/%d line-editing tests passed" % (len(tests) - failed, len(tests)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
