// Unit tests for the parser (tokens -> Command). Build and run with: make test
//
// Each case: shell input -> a text description of the resulting Command, or a
// syntax error. Redirections print as <fd><operator><path>, for example
// "1>out", "1>>log", "0<in", "2>err".

#include "lexer.h"
#include "parser.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct Case {
    std::string input;
    std::string expected;  // description of the Command, or "ERROR"
};

std::string describe(const Command& command) {
    std::string text = "[";
    for (size_t i = 0; i < command.argv.size(); ++i) {
        text += (i == 0 ? "" : "|") + command.argv[i];
    }
    text += "]";
    for (const Redirection& r : command.redirections) {
        const char* op = r.kind == Redirection::Kind::Read     ? "<"
                         : r.kind == Redirection::Kind::Append ? ">>"
                                                               : ">";
        text += " " + std::to_string(r.fd) + op + r.path;
    }
    return text;
}

// A pipeline prints its commands joined by " | ". A blank line prints "[]".
std::string describe(const Pipeline& pipeline) {
    if (pipeline.commands.empty()) {
        return "[]";
    }
    std::string text;
    for (size_t i = 0; i < pipeline.commands.size(); ++i) {
        text += (i == 0 ? "" : " | ") + describe(pipeline.commands[i]);
    }
    return text;
}

// Run the real pipeline: tokenize, then parse.
std::string run(const std::string& line) {
    std::vector<Token> tokens;
    Pipeline pipeline;
    std::string error;
    if (!tokenize(line, 0, tokens, error) || !parse_pipeline(tokens, pipeline, error)) {
        return "ERROR";
    }
    return describe(pipeline);
}

}  // namespace

int main() {
    setenv("NX_FILE", "from-env", 1);

    const std::vector<Case> cases = {
        // --- no redirection ---
        {"ls -l /tmp", "[ls|-l|/tmp]"},
        {"", "[]"},

        // --- each operator and its descriptor ---
        {"echo hi > out", "[echo|hi] 1>out"},
        {"echo hi >> log", "[echo|hi] 1>>log"},
        {"cat < in", "[cat] 0<in"},
        {"ls /nope 2> err", "[ls|/nope] 2>err"},

        // --- no spaces around the operator ---
        {"echo hi>out", "[echo|hi] 1>out"},
        {"cat<in>out", "[cat] 0<in 1>out"},
        {"ls 2>err", "[ls] 2>err"},

        // --- several redirections keep their written order ---
        {"sort < in > out", "[sort] 0<in 1>out"},
        {"cmd > a > b", "[cmd] 1>a 1>b"},
        {"cmd 2> e > o < i", "[cmd] 2>e 1>o 0<i"},
        {"cmd >> a 2> b", "[cmd] 1>>a 2>b"},

        // --- redirections may come before or between words ---
        {"> out echo hi", "[echo|hi] 1>out"},
        {"echo > out hi", "[echo|hi] 1>out"},
        {"< in cat -n", "[cat|-n] 0<in"},

        // --- only redirections: no command, files still get opened ---
        {"> out", "[] 1>out"},
        {"< in > out", "[] 0<in 1>out"},

        // --- "2" handling ---
        {"echo 2 > f", "[echo|2] 1>f"},
        {"echo 2>f", "[echo] 2>f"},
        {"echo a2>f", "[echo|a2] 1>f"},

        // --- quoted operators are arguments, not redirections ---
        {R"X(echo ">" "<" ">>")X", "[echo|>|<|>>]"},
        {R"X(echo hi > ">")X", "[echo|hi] 1>>"},  // a file literally named ">"
        {R"X(echo hi > "my file")X", "[echo|hi] 1>my file"},
        {"echo hi > $NX_FILE", "[echo|hi] 1>from-env"},

        // --- pipelines ---
        {"ls | wc", "[ls] | [wc]"},
        {"ls|wc", "[ls] | [wc]"},
        {"ls -l | grep cpp | wc -l", "[ls|-l] | [grep|cpp] | [wc|-l]"},
        {"a | b | c | d | e", "[a] | [b] | [c] | [d] | [e]"},
        {"cat < in | sort > out", "[cat] 0<in | [sort] 1>out"},
        {"echo hi > f | cat", "[echo|hi] 1>f | [cat]"},
        {"ls 2> e | wc < in", "[ls] 2>e | [wc] 0<in"},
        {"> out echo hi | cat", "[echo|hi] 1>out | [cat]"},
        {R"X(echo "a|b" | cat)X", "[echo|a|b] | [cat]"},
        {R"X(echo a\|b)X", "[echo|a|b]"},
        {R"X(echo "|" | cat)X", "[echo||] | [cat]"},
        {"echo $NX_FILE | cat", "[echo|from-env] | [cat]"},

        // --- errors: every stage of a pipeline needs a command ---
        {"| cat", "ERROR"},
        {"echo |", "ERROR"},
        {"echo || cat", "ERROR"},
        {"ls | | wc", "ERROR"},
        {"|", "ERROR"},
        {"| |", "ERROR"},
        {"> f | cat", "ERROR"},
        {"echo | > f", "ERROR"},
        {"echo > | cat", "ERROR"},
        {"echo | cat <", "ERROR"},

        // --- errors: operator without a file name ---
        {"echo hi >", "ERROR"},
        {"echo hi >>", "ERROR"},
        {"cat <", "ERROR"},
        {"ls 2>", "ERROR"},
        {"echo > > f", "ERROR"},
        {"echo > < f", "ERROR"},
        {"echo >>> f", "ERROR"},
        {"echo 2>>f", "ERROR"},  // 2>> is not supported yet
        {">", "ERROR"},

        // --- errors: descriptor duplication is not supported ---
        {"ls 2>&1", "ERROR"},
        {"ls > &2", "ERROR"},
    };

    int failures = 0;
    for (const Case& c : cases) {
        std::string got = run(c.input);
        if (got != c.expected) {
            ++failures;
            std::cout << "FAIL: " << c.input << "\n"
                      << "   expected: " << c.expected << "\n"
                      << "   got:      " << got << "\n";
        }
    }

    std::cout << (cases.size() - failures) << "/" << cases.size()
              << " parser tests passed" << std::endl;
    return failures == 0 ? 0 : 1;
}
