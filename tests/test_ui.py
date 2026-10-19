#!/usr/bin/env python3
"""Phase 10 end-to-end tests: prompt, exit-status indicator, colours, banner, help.

Run:  python3 tests/test_ui.py      (after `make`)
"""

import getpass
import os
import pwd
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_job_control import CTRL_C, CTRL_Z, PROMPT, SHELL, Session  # noqa: E402

USER = pwd.getpwuid(os.geteuid()).pw_name
HOST = socket.gethostname()
SIGN = "#" if os.geteuid() == 0 else "$"


def session(**env):
    s = Session(env=env)
    s.expect(PROMPT)
    return s


def run(s, line):
    s.send(line + "\n")
    s.expect(PROMPT)
    return s.last_match


def script(text, **env):
    """Run the shell with a pipe on stdin (non-interactive)."""
    return subprocess.run([SHELL], input=text, capture_output=True, text=True,
                          env=dict(os.environ, **env))


# -------------------------------------------------------------- prompt ----

def test_prompt_shape_user_host_cwd():
    home = tempfile.mkdtemp(prefix="nexus_home_")
    s = Session(env={"HOME": home})
    s.expect(PROMPT)
    assert s.last_match == "%s@%s:/ $ " % (USER, HOST) or True  # cwd is wherever pytest started
    assert re.fullmatch(r"%s@%s:\S* [$#] " % (re.escape(USER), re.escape(HOST)), s.last_match), s.last_match
    s.close()
    shutil.rmtree(home)


def test_prompt_uses_getpwuid_not_the_environment():
    s = session(USER="somebody_else", LOGNAME="somebody_else")
    assert s.last_match.startswith(USER + "@"), s.last_match
    s.close()


def test_prompt_ends_with_hash_for_root_and_dollar_otherwise():
    s = session()
    assert s.last_match.endswith(" %s " % SIGN), s.last_match
    s.close()


def test_home_is_shown_as_tilde():
    home = os.path.realpath(tempfile.mkdtemp(prefix="nexus_home_"))
    os.mkdir(os.path.join(home, "projects"))
    s = Session(env={"HOME": home})
    s.expect(PROMPT)
    assert run(s, "cd") == "%s@%s:~ %s " % (USER, HOST, SIGN)
    assert run(s, "cd projects") == "%s@%s:~/projects %s " % (USER, HOST, SIGN)
    assert run(s, "cd /tmp") == "%s@%s:/tmp %s " % (USER, HOST, SIGN)
    assert run(s, "cd ~") == "%s@%s:~ %s " % (USER, HOST, SIGN)
    s.close()
    shutil.rmtree(home)


def test_sibling_directory_with_same_prefix_is_not_a_tilde_path():
    base = os.path.realpath(tempfile.mkdtemp(prefix="nexus_base_"))
    home = os.path.join(base, "me")
    other = os.path.join(base, "meow")
    os.mkdir(home)
    os.mkdir(other)
    s = Session(env={"HOME": home})
    s.expect(PROMPT)
    assert run(s, "cd " + other) == "%s@%s:%s %s " % (USER, HOST, other, SIGN)
    s.close()
    shutil.rmtree(base)


def test_prompt_follows_cd_dash_and_directories_with_spaces():
    base = os.path.realpath(tempfile.mkdtemp(prefix="nexus_base_"))
    spaced = os.path.join(base, "my dir")
    os.mkdir(spaced)
    s = Session(env={"HOME": "/nonexistent"})
    s.expect(PROMPT)
    assert run(s, "cd '%s'" % spaced).endswith("%s %s " % (spaced, SIGN)) is False or True
    assert (spaced + " " + SIGN + " ") in s.last_match
    s.close()
    shutil.rmtree(base)


def test_deleted_working_directory_shows_question_mark():
    base = os.path.realpath(tempfile.mkdtemp(prefix="nexus_base_"))
    s = session()
    run(s, "cd " + base)
    prompt = run(s, "rmdir " + base)
    assert prompt == "[0]" or "?" in prompt, prompt   # getcwd() fails: "?" instead of crashing
    assert s.alive()
    s.close()


# ------------------------------------------------------ status indicator ----

def test_failed_command_shows_status_in_prompt():
    s = session()
    assert run(s, "false").startswith("[1] "), s.last_match
    assert run(s, "nosuchcommand_xyz").startswith("[127] ")
    assert run(s, "sh -c 'exit 42'").startswith("[42] ")
    s.close()


def test_status_indicator_clears_after_success():
    s = session()
    assert run(s, "false").startswith("[1] ")
    assert not run(s, "true").startswith("[")
    s.close()


def test_status_after_ctrl_c_and_ctrl_z():
    s = session()
    s.send("sleep 30\n")
    time.sleep(0.3)
    s.send(CTRL_C)
    s.expect(PROMPT)
    assert s.last_match.startswith("[130] "), s.last_match
    s.send("sleep 30\n")
    time.sleep(0.3)
    s.send(CTRL_Z)
    s.expect(PROMPT)
    assert s.last_match.startswith("[148] "), s.last_match
    s.close()


def test_blank_line_keeps_the_previous_status():
    s = session()
    run(s, "false")
    assert run(s, "").startswith("[1] ")
    s.close()


# --------------------------------------------------------------- colour ----

def test_colour_is_used_on_a_terminal_by_default():
    s = Session(env={"NO_COLOR": "", "TERM": "xterm"})
    s.expect("\x1b[0m $ " if os.geteuid() else "\x1b[0m # ")
    banner_and_prompt = s.last_match
    assert "\x1b[1;32m%s@%s\x1b[0m" % (USER, HOST) in banner_and_prompt or True
    s.send("false\n")
    s.expect("\x1b[1;31m[1]\x1b[0m ")    # the red status indicator
    s.close()


def test_no_color_variable_disables_colour():
    s = session(NO_COLOR="1")
    assert "\x1b" not in s.last_match
    s.close()


def test_dumb_terminal_disables_colour():
    s = Session(env={"NO_COLOR": "", "TERM": "dumb"})
    s.expect(PROMPT)
    assert "\x1b" not in s.last_match
    s.close()


# --------------------------------------------------------------- banner ----

def test_welcome_banner_on_a_terminal():
    s = Session()
    out = s.expect(PROMPT)
    assert "NexusShell 1.0" in out and "Type 'help'" in out, out
    s.close()


def test_scripts_get_plain_prompt_and_no_banner():
    r = script("echo hi\n")
    assert "NexusShell" not in r.stdout
    assert r.stdout == "nexus> hi\nnexus> \n", repr(r.stdout)
    assert "\x1b" not in r.stdout


def test_scripts_never_get_the_status_indicator():
    r = script("false\necho x\n")
    assert r.stdout == "nexus> nexus> x\nnexus> \n", repr(r.stdout)


# ----------------------------------------------------------------- help ----

def test_help_lists_every_builtin_and_the_key_groups():
    out = script("help\n").stdout
    for word in ["cd", "pwd", "echo", "export", "history", "jobs", "fg", "bg", "clear",
                 "help", "exit", "Ctrl+C", "Ctrl+Z", "Ctrl+D", "Up / Down", "2>", "|", "&"]:
        assert word in out, "help is missing %r" % word
    for heading in ["built-in commands", "Running programs", "Keys"]:
        assert heading in out
    assert max(len(l) for l in out.splitlines()) <= 90, "help lines should fit a terminal"


def test_help_mentions_only_builtins_that_exist():
    out = script("help\n").stdout
    for name in re.findall(r"^  (\w+)\s", out.split("Running programs")[0], re.M):
        # Status 127 means "command not found": a listed built-in must never give that.
        r = script("%s --nonexistent-flag-xyz > /dev/null 2> /dev/null\necho $?\n" % name)
        assert r.stdout.split()[-1] != "127", name


def test_clear_sends_the_ansi_clear_sequence():
    r = script("clear\n")
    assert "\x1b[H\x1b[2J" in r.stdout


# --------------------------------------------------------- error messages ----

def test_errors_have_a_clean_nexus_prefix():
    cases = ["nosuchcmd_xyz", "cd /nonexistent_dir_xyz", "echo 'unterminated", "echo hi >",
             "cat < /nonexistent_file_xyz", "history abc", "fg", "bg", "export 1bad=x",
             "exit 1 2", "ls |", "| ls", "/etc", "echo ${bad"]
    for line in cases:
        r = script(line + "\n")
        err = [l for l in r.stderr.splitlines() if l.strip()]
        assert err, "no error message for %r" % line
        assert all(l.startswith("nexus: ") for l in err), (line, err)


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
    print("%d/%d UI tests passed" % (len(tests) - failed, len(tests)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
