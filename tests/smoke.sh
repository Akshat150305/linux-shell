#!/usr/bin/env bash
# End-to-end smoke test: feeds scripts to the real ./nexusshell through a pipe
# and compares what comes out. Covers every feature once, quickly (about 5 s).
#
#   bash tests/smoke.sh [path-to-nexusshell]
#
# Prompts ("nexus> ") are stripped and PIDs are replaced by PID so the expected
# text is stable. Pseudo-terminal tests (Ctrl+C, arrows, ...) are in the .py files.

SHELL_BIN=${1:-./nexusshell}
T=$(mktemp -d)                  # scratch directory, visible to the shell as $T
export T
trap 'rm -rf "$T"' EXIT

pass=0
fail=0

# Run a script in the shell; stdout and stderr together, prompts removed.
run() {
    printf '%s\n' "$1" | timeout 20 "$SHELL_BIN" 2>&1 \
        | sed -E 's/nexus> //g; s/^\[([0-9]+)\] [0-9]+$/[\1] PID/'
}

check() {   # check "name" "script" "expected output"
    local actual
    actual=$(run "$2")
    if [ "$actual" == "$3" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        printf 'FAIL  %s\n  expected: %s\n  actual:   %s\n' "$1" "$3" "$actual"
    fi
}

nl=$'\n'

# --- core: commands, quoting, variables ---
check "external command"        'echo hello world'                    'hello world'
check "quotes and escapes"      'echo "a  b" '"'"'c  d'"'"' e\ f'     'a  b c  d e f'
check "variables"               "export X=42${nl}"'echo $X ${X}z "$X"'  '42 42z 42'
check "unset variable vanishes" 'echo a $NOPE_XYZ b'                  'a b'
check "tilde"                   'cd /tmp'"${nl}"'pwd'                  '/tmp'
check "exit status 127"         "nosuchcmd_xyz${nl}"'echo $?'         "nexus: nosuchcmd_xyz: command not found${nl}127"
check "exit status 1"           "false${nl}"'echo $?'                 '1'
check "syntax error is 2"       "echo \"open${nl}"'echo $?'           "nexus: syntax error: unterminated double quote${nl}2"
check "cd error"                'cd /nonexistent_xyz'                 'nexus: cd: /nonexistent_xyz: No such file or directory'

# --- redirection ---
check "> and >> and <"          'echo one > $T/f'"${nl}"'echo two >> $T/f'"${nl}"'cat < $T/f' "one${nl}two"
check "2> captures stderr"      'ls /nonexistent_xyz 2> $T/e'"${nl}"'cat $T/e | wc -l'  '1'
check "missing input file"      'cat < /nonexistent_xyz'              'nexus: /nonexistent_xyz: No such file or directory'
check "builtin redirected"      'echo hi > $T/g'"${nl}"'pwd > $T/h'"${nl}"'cat $T/g'    'hi'

# --- pipes ---
check "three-stage pipe"        "echo 'b a c' | tr ' ' '\\n' | sort | head -n 2"  "a${nl}b"
check "builtin in a pipe"       'echo hi | cat'                       'hi'
check "pipe status is last"     'false | true'"${nl}"'echo $?'        '0'
check "big pipe, no deadlock"   'seq 1 100000 | wc -l'               '100000'
check "SIGPIPE ends producer"   'yes | head -n 1'                     'y'
# CI runners and many launchers start programs with SIGPIPE ignored, and ignored signals
# survive exec. The shell must give its children the default action back.
actual=$(trap '' PIPE; run 'yes | head -n 1')
if [ "$actual" == "y" ]; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL  SIGPIPE ignored by the parent: got '$actual'"; fi

# --- background jobs, zombies, descriptors ---
check "background + Done"       "sleep 0.1 &${nl}sleep 0.4"           "[1] PID${nl}[1]+  Done       sleep 0.1"
check "no zombies"              "sleep 0.1 &${nl}sleep 0.4${nl}"'ps -o stat= --ppid $$ | grep -c Z'  "[1] PID${nl}[1]+  Done       sleep 0.1${nl}0"
# Compare the shell's open descriptors before and after a workload. (A fixed count would
# break whenever the test runner passes down extra descriptors, as ctest does.) The listing
# goes to a file, not a pipe: with `ls | wc` the shell itself holds the pipe for a moment.
check "no leaked descriptors"   'ls /proc/$$/fd > $T/before'"${nl}"'echo hi | cat > /dev/null'"${nl}"'echo x > $T/o'"${nl}"'sleep 0.1 &'"${nl}"'sleep 0.3'"${nl}"'cat < $T/o | cat > /dev/null'"${nl}"'ls /proc/$$/fd > $T/after'"${nl}"'cmp -s $T/before $T/after'"${nl}"'echo $?'  "[1] PID${nl}[1]+  Done       sleep 0.1${nl}0"
check "fg needs a terminal"     'fg'                                  'nexus: fg: no job control'
many=""
for _ in $(seq 150); do many+="true | true | true${nl}"; done
check "150 pipelines"           "${many}echo done"                    'done'

# --- history (in memory when not a terminal) ---
check "history list"            "echo a${nl}echo b${nl}history"       "a${nl}b${nl}    1  echo a${nl}    2  echo b${nl}    3  history"
check "!! re-runs"              "echo a${nl}!!"                       "a${nl}echo a${nl}a"
# scripts must leave the history file alone
NEXUSSHELL_HISTFILE="$T/hist" run 'echo x' > /dev/null
if [ -e "$T/hist" ]; then fail=$((fail + 1)); echo "FAIL  scripts must not write the history file"; else pass=$((pass + 1)); fi

# --- the shell's own exit status ---
printf 'exit 7\n' | timeout 5 "$SHELL_BIN" > /dev/null 2>&1
if [ $? -eq 7 ]; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL  exit 7 should end the shell with status 7"; fi

printf 'false\n' | timeout 5 "$SHELL_BIN" > /dev/null 2>&1
if [ $? -eq 1 ]; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL  EOF should end the shell with the last status"; fi

echo "$pass/$((pass + fail)) smoke checks passed"
[ "$fail" -eq 0 ]
