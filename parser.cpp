// NexusShell - parser.cpp
// Words become argv, operator + file name pairs become redirections, '|'
// splits a line into pipeline stages, and a trailing '&' marks it background.

#include "parser.h"

namespace {

bool is_operator(const Token& token, const char* text) {
    return token.is_operator && token.text == text;
}

}  // namespace

bool parse_command(const std::vector<Token>& tokens, Command& command,
                   std::string& error) {
    command = Command{};
    error.clear();

    for (size_t i = 0; i < tokens.size(); ++i) {
        const Token& token = tokens[i];

        if (!token.is_operator) {
            command.argv.push_back(token.text);
            continue;
        }

        // parse_pipeline already removed a trailing '&'; any other one has
        // something after it, and we have no command lists (`a & b`) yet.
        if (token.text == "&") {
            error = "'&' is only supported at the end of a command line";
            return false;
        }

        // `2>&1`: the lexer sees the redirection followed by the '&' operator.
        if (i + 1 < tokens.size() && is_operator(tokens[i + 1], "&")) {
            error = "'" + token.text +
                    "&' (descriptor duplication) is not supported yet";
            return false;
        }

        // A redirection operator must be followed by a file name (a word).
        if (i + 1 >= tokens.size() || tokens[i + 1].is_operator) {
            error = "expected a file name after '" + token.text + "'";
            return false;
        }
        const std::string& path = tokens[i + 1].text;
        ++i;  // consume the file name

        Redirection redirection;
        redirection.path = path;
        if (token.text == "<") {
            redirection.fd = 0;
            redirection.kind = Redirection::Kind::Read;
        } else if (token.text == ">") {
            redirection.fd = 1;
            redirection.kind = Redirection::Kind::Write;
        } else if (token.text == ">>") {
            redirection.fd = 1;
            redirection.kind = Redirection::Kind::Append;
        } else if (token.text == "2>") {
            redirection.fd = 2;
            redirection.kind = Redirection::Kind::Write;
        } else {
            error = "unexpected operator '" + token.text + "'";
            return false;
        }
        command.redirections.push_back(redirection);
    }
    return true;
}

bool parse_pipeline(const std::vector<Token>& tokens, Pipeline& pipeline,
                    std::string& error) {
    pipeline = Pipeline{};
    error.clear();
    if (tokens.empty()) {
        return true;  // nothing to run
    }

    // A trailing '&' means "run in the background"; the rest is parsed as usual.
    size_t end = tokens.size();
    if (is_operator(tokens.back(), "&")) {
        pipeline.background = true;
        --end;
        if (end == 0) {
            error = "missing command before '&'";
            return false;
        }
    }

    // Collect tokens until a '|', parse them as one command, start over.
    std::vector<Token> segment;
    auto finish_segment = [&]() {
        Command command;
        if (!parse_command(segment, command, error)) {
            return false;
        }
        pipeline.commands.push_back(command);
        segment.clear();
        return true;
    };

    bool has_pipe = false;
    for (size_t i = 0; i < end; ++i) {
        if (is_operator(tokens[i], "|")) {
            has_pipe = true;
            if (!finish_segment()) {
                return false;
            }
        } else {
            segment.push_back(tokens[i]);
        }
    }
    if (!finish_segment()) {
        return false;
    }

    // Every stage of a real pipeline must be a command, and so must a
    // background line (there is nothing to run for `> f &`).
    if (has_pipe || pipeline.background) {
        for (const Command& command : pipeline.commands) {
            if (command.argv.empty()) {
                error = has_pipe ? "missing command next to '|'"
                                 : "missing command before '&'";
                return false;
            }
        }
    }
    return true;
}
