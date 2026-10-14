// Unit tests for the lexer. Build and run with: make test
//
// Each case is: shell input -> the exact argument list the program would get
// (or a syntax error). The lexer is a pure function, so no shell is needed.

#include "lexer.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

struct Case {
    std::string input;
    Args expected;
    bool expect_error;
};

std::string show(const Args& words) {
    std::string text = "[";
    for (size_t i = 0; i < words.size(); ++i) {
        text += (i == 0 ? "" : " | ") + words[i];
    }
    return text + "]";
}

}  // namespace

int main() {
    // Fixed environment so expansion results are predictable.
    setenv("HOME", "/home/nx", 1);
    setenv("NX_VAR", "value", 1);
    setenv("NX_SPACED", "a b", 1);
    unsetenv("NX_UNSET");

    const int status = 42;  // what $? expands to in these tests
    const std::string pid = std::to_string(static_cast<long>(getpid()));

    const std::vector<Case> cases = {
        // --- plain words and whitespace ---
        {"ls -l /tmp", {"ls", "-l", "/tmp"}, false},
        {"  echo   a    b  ", {"echo", "a", "b"}, false},
        {"echo\ta", {"echo", "a"}, false},
        {"", {}, false},
        {"   \t  ", {}, false},

        // --- single quotes: everything literal ---
        {R"X(echo 'a  b' c)X", {"echo", "a  b", "c"}, false},
        {R"X(echo 'single $NX_VAR "x" \n')X", {"echo", R"X(single $NX_VAR "x" \n)X"}, false},
        {R"X(echo '')X", {"echo", ""}, false},
        {R"X(echo '' x)X", {"echo", "", "x"}, false},

        // --- double quotes: spaces kept, $ expands, \" \\ \$ are escapes ---
        {R"X(echo "hello world")X", {"echo", "hello world"}, false},
        {R"X(echo "a  b"   c)X", {"echo", "a  b", "c"}, false},
        {R"X(echo "say \"hi\"")X", {"echo", R"X(say "hi")X"}, false},
        {R"X(echo "back\\slash")X", {"echo", R"X(back\slash)X"}, false},
        {R"X(echo "keep\nliteral")X", {"echo", R"X(keep\nliteral)X"}, false},
        {R"X(echo "it's")X", {"echo", "it's"}, false},
        {R"X(echo "")X", {"echo", ""}, false},
        {R"X(echo "$NX_VAR and $NX_SPACED")X", {"echo", "value and a b"}, false},

        // --- quoted and unquoted pieces concatenate into one word ---
        {R"X(echo ab"cd"'ef'gh)X", {"echo", "abcdefgh"}, false},
        {R"X(echo 'it'"'"'s')X", {"echo", "it's"}, false},

        // --- backslash outside quotes ---
        {R"X(echo a\ b)X", {"echo", "a b"}, false},
        {R"X(echo \"hi\")X", {"echo", R"X("hi")X"}, false},
        {R"X(echo \$NX_VAR)X", {"echo", "$NX_VAR"}, false},
        {R"X(echo a\\b)X", {"echo", R"X(a\b)X"}, false},
        {R"X(echo \'x\')X", {"echo", "'x'"}, false},

        // --- variable expansion ---
        {"echo $NX_VAR", {"echo", "value"}, false},
        {R"X(echo "$NX_VAR/dir")X", {"echo", "value/dir"}, false},
        {"echo ${NX_VAR}x", {"echo", "valuex"}, false},
        {"echo $NX_VAR-x", {"echo", "value-x"}, false},
        {"echo $NX_VAR$NX_VAR", {"echo", "valuevalue"}, false},
        {"echo $NX_SPACED", {"echo", "a b"}, false},  // no word splitting
        {"echo $?", {"echo", "42"}, false},
        {"echo $$", {"echo", pid}, false},
        {R"X(echo '$NX_VAR')X", {"echo", "$NX_VAR"}, false},
        {R"X(echo "\$NX_VAR")X", {"echo", "$NX_VAR"}, false},

        // --- unset variables: unquoted vanishes, quoted stays as "" ---
        {"echo $NX_UNSET", {"echo"}, false},
        {"echo a $NX_UNSET b", {"echo", "a", "b"}, false},
        {R"X(echo "$NX_UNSET")X", {"echo", ""}, false},
        {"echo x$NX_UNSET", {"echo", "x"}, false},
        {"$NX_UNSET", {}, false},

        // --- a '$' that starts no valid reference is literal ---
        {"echo $", {"echo", "$"}, false},
        {"echo $ x", {"echo", "$", "x"}, false},
        {"echo $1", {"echo", "$1"}, false},
        {"echo a$", {"echo", "a$"}, false},
        {R"X(echo "cost: $")X", {"echo", "cost: $"}, false},

        // --- tilde: only unquoted, at the start of a word ---
        {"cd ~", {"cd", "/home/nx"}, false},
        {"cd ~/src", {"cd", "/home/nx/src"}, false},
        {"echo x ~", {"echo", "x", "/home/nx"}, false},
        {"echo ~x", {"echo", "~x"}, false},
        {"echo a~", {"echo", "a~"}, false},
        {R"X(echo "~")X", {"echo", "~"}, false},
        {R"X(echo '~')X", {"echo", "~"}, false},
        {R"X(echo \~)X", {"echo", "~"}, false},

        // --- syntax errors ---
        {R"X(echo "abc)X", {}, true},
        {R"X(echo 'abc)X", {}, true},
        {R"X(echo abc\)X", {}, true},
        {R"X(echo "ends with backslash\)X", {}, true},
        {R"X(echo "unterminated $NX_VAR)X", {}, true},
        {"echo ${NX_VAR", {}, true},
        {"echo ${}", {}, true},
        {"echo ${1BAD}", {}, true},
    };

    int failures = 0;
    for (const Case& c : cases) {
        Args words;
        std::string error;
        bool ok = tokenize(c.input, status, words, error);
        bool pass = c.expect_error ? (!ok && !error.empty()) : (ok && words == c.expected);
        if (!pass) {
            ++failures;
            std::cout << "FAIL: " << c.input << "\n"
                      << "   expected: "
                      << (c.expect_error ? std::string("<syntax error>") : show(c.expected)) << "\n"
                      << "   got:      "
                      << (ok ? show(words) : "<error: " + error + ">") << "\n";
        }
    }

    std::cout << (cases.size() - failures) << "/" << cases.size()
              << " lexer tests passed" << std::endl;
    return failures == 0 ? 0 : 1;
}
