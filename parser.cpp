// NexusShell - parser.cpp
// Phase 5: words become argv, operator + file name pairs become redirections.

#include "parser.h"

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

        // An operator must be followed by a file name (a word).
        if (i + 1 >= tokens.size() || tokens[i + 1].is_operator) {
            error = "expected a file name after '" + token.text + "'";
            return false;
        }
        const std::string& path = tokens[i + 1].text;
        ++i;  // consume the file name

        // `2>&1` would otherwise silently create a file called "&1".
        if (!path.empty() && path[0] == '&') {
            error = "'" + token.text + path +
                    "' (descriptor duplication) is not supported yet";
            return false;
        }

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
    for (const Token& token : tokens) {
        if (token.is_operator && token.text == "|") {
            has_pipe = true;
            if (!finish_segment()) {
                return false;
            }
        } else {
            segment.push_back(token);
        }
    }
    if (!finish_segment()) {
        return false;
    }

    // Every stage of a real pipeline must be a command.
    if (has_pipe) {
        for (const Command& command : pipeline.commands) {
            if (command.argv.empty()) {
                error = "missing command next to '|'";
                return false;
            }
        }
    }
    return true;
}
