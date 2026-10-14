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
        } else {  // "2>"
            redirection.fd = 2;
            redirection.kind = Redirection::Kind::Write;
        }
        command.redirections.push_back(redirection);
    }
    return true;
}
