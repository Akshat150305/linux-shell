// NexusShell - lexer.cpp
// Phase 4: quotes, escapes, $VAR expansion, ~ expansion.
//
// The lexer is a single left-to-right pass over the line. It keeps the word
// being built in `word`; quote and escape handling appends to it, and
// whitespace outside quotes finishes it.

#include "lexer.h"

#include <cctype>
#include <cstdlib>
#include <string>

#include <unistd.h>  // getpid

namespace {

bool is_name_start(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    return std::isalpha(u) || u == '_';
}

bool is_name_char(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) || u == '_';
}

// Value of an environment variable; unset variables expand to "".
std::string env_value(const std::string& name) {
    const char* value = std::getenv(name.c_str());
    return value != nullptr ? value : "";
}

// Expand the reference that starts at line[i] == '$' and append the result
// to `out`. Advances `i` past the reference. Supported forms:
//   $NAME   ${NAME}   $?   $$
// A '$' followed by anything else is kept as a literal '$'.
// Returns false (and sets `error`) for a malformed ${...}.
bool expand_dollar(const std::string& line, size_t& i, int last_status,
                   std::string& out, std::string& error) {
    char next = (i + 1 < line.size()) ? line[i + 1] : '\0';

    if (next == '?') {
        out += std::to_string(last_status);
        i += 2;
    } else if (next == '$') {
        out += std::to_string(static_cast<long>(getpid()));
        i += 2;
    } else if (next == '{') {
        size_t close = line.find('}', i + 2);
        if (close == std::string::npos) {
            error = "unterminated '${'";
            return false;
        }
        std::string name = line.substr(i + 2, close - (i + 2));
        bool valid = !name.empty() && is_name_start(name[0]);
        for (char c : name) {
            valid = valid && is_name_char(c);
        }
        if (!valid) {
            error = "bad substitution: ${" + name + "}";
            return false;
        }
        out += env_value(name);
        i = close + 1;
    } else if (is_name_start(next)) {
        size_t end = i + 1;
        while (end < line.size() && is_name_char(line[end])) {
            ++end;
        }
        out += env_value(line.substr(i + 1, end - (i + 1)));
        i = end;
    } else {
        out += '$';  // lone '$': literal
        i += 1;
    }
    return true;
}

}  // namespace

bool tokenize(const std::string& line, int last_status, std::vector<Token>& tokens,
              std::string& error) {
    tokens.clear();
    error.clear();

    std::string word;
    bool in_word = false;  // are we in the middle of a word?
    bool quoted = false;   // did this word contain quotes? (then "" is a real argument)

    // A word is kept if it has content or was quoted. An unquoted expansion
    // that produced nothing (e.g. $UNSET) therefore vanishes, while "" stays.
    auto finish_word = [&]() {
        if (in_word && (quoted || !word.empty())) {
            tokens.push_back(Token{word, false});
        }
        word.clear();
        in_word = false;
        quoted = false;
    };

    size_t i = 0;
    while (i < line.size()) {
        char c = line[i];

        // Whitespace outside quotes ends the current word.
        if (c == ' ' || c == '\t' || c == '\n') {
            finish_word();
            ++i;
            continue;
        }

        // Redirection operators, only when unquoted: <  >  >>  and 2>
        // "2>" counts only at the start of a word, so `a2>f` is the word
        // "a2" followed by ">", and `echo 2 > f` passes "2" as an argument.
        char ahead = (i + 1 < line.size()) ? line[i + 1] : '\0';
        if (c == '<' || c == '>' || (c == '2' && !in_word && ahead == '>')) {
            finish_word();  // an operator also ends the word before it: echo hi>f
            std::string op;
            if (c == '2') {
                op = "2>";
                i += 2;
            } else if (c == '>' && ahead == '>') {
                op = ">>";
                i += 2;
            } else {
                op = std::string(1, c);
                i += 1;
            }
            tokens.push_back(Token{op, true});
            continue;
        }

        bool word_start = !in_word;
        in_word = true;

        // ~ at the start of a word, followed by '/' or the end of the word.
        if (c == '~' && word_start) {
            char next = (i + 1 < line.size()) ? line[i + 1] : ' ';
            const char* home = std::getenv("HOME");
            if ((next == '/' || next == ' ' || next == '\t') && home != nullptr &&
                *home != '\0') {
                word += home;
                ++i;
                continue;
            }
        }

        // Backslash outside quotes: the next character is taken literally.
        if (c == '\\') {
            if (i + 1 >= line.size()) {
                error = "unexpected end of line after '\\'";
                return false;
            }
            word += line[i + 1];
            i += 2;
            continue;
        }

        // Single quotes: everything up to the closing quote is literal.
        if (c == '\'') {
            size_t close = line.find('\'', i + 1);
            if (close == std::string::npos) {
                error = "unterminated single quote";
                return false;
            }
            word.append(line, i + 1, close - (i + 1));
            quoted = true;
            i = close + 1;
            continue;
        }

        // Double quotes: literal, except $ expansion and \" \\ \$ escapes.
        if (c == '"') {
            quoted = true;
            ++i;
            bool closed = false;
            while (i < line.size()) {
                char d = line[i];
                if (d == '"') {
                    closed = true;
                    ++i;
                    break;
                }
                if (d == '\\' && i + 1 < line.size()) {
                    char n = line[i + 1];
                    if (n == '"' || n == '\\' || n == '$') {
                        word += n;
                        i += 2;
                        continue;
                    }
                    // any other backslash stays in the text, as in bash
                }
                if (d == '$') {
                    if (!expand_dollar(line, i, last_status, word, error)) {
                        return false;
                    }
                    continue;
                }
                word += d;
                ++i;
            }
            if (!closed) {
                error = "unterminated double quote";
                return false;
            }
            continue;
        }

        // Unquoted $ expansion.
        if (c == '$') {
            if (!expand_dollar(line, i, last_status, word, error)) {
                return false;
            }
            continue;
        }

        // Ordinary character.
        word += c;
        ++i;
    }

    finish_word();
    return true;
}
