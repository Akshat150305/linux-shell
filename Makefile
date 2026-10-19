CXX      := g++
CXXFLAGS := -Wall -Wextra -std=c++17 -g
TARGET   := nexusshell
SRCS     := main.cpp builtins.cpp lexer.cpp parser.cpp redirect.cpp executor.cpp jobs.cpp signals.cpp history.cpp input.cpp prompt.cpp
OBJS     := $(SRCS:.cpp=.o)
HEADERS  := builtins.h lexer.h parser.h redirect.h executor.h jobs.h signals.h history.h input.h prompt.h
TEST_BINS := tests/test_lexer tests/test_parser tests/test_jobs tests/test_history tests/test_prompt

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@

# Rebuild an object file when its .cpp or any header changes.
%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Unit tests (no shell needed). `make test-pty` drives the real shell on a
# pseudo-terminal to test Ctrl+C / Ctrl+Z / fg / bg.
test: $(TEST_BINS)
	./tests/test_lexer
	./tests/test_parser
	./tests/test_jobs
	./tests/test_history
	./tests/test_prompt

tests/test_lexer: tests/test_lexer.cpp lexer.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -I. tests/test_lexer.cpp lexer.cpp -o $@

tests/test_parser: tests/test_parser.cpp lexer.cpp parser.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -I. tests/test_parser.cpp lexer.cpp parser.cpp -o $@

tests/test_jobs: tests/test_jobs.cpp jobs.cpp signals.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -I. tests/test_jobs.cpp jobs.cpp signals.cpp -o $@

tests/test_history: tests/test_history.cpp history.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -I. tests/test_history.cpp history.cpp -o $@

tests/test_prompt: tests/test_prompt.cpp prompt.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -I. tests/test_prompt.cpp prompt.cpp -o $@

test-pty: $(TARGET)
	python3 tests/test_job_control.py
	python3 tests/test_line_editing.py
	python3 tests/test_ui.py

clean:
	rm -f $(OBJS) $(TARGET) $(TEST_BINS)

.PHONY: all test test-pty clean
