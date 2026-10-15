CXX      := g++
CXXFLAGS := -Wall -Wextra -std=c++17 -g
TARGET   := nexusshell
SRCS     := main.cpp builtins.cpp lexer.cpp parser.cpp redirect.cpp executor.cpp jobs.cpp
OBJS     := $(SRCS:.cpp=.o)
HEADERS  := builtins.h lexer.h parser.h redirect.h executor.h jobs.h
TEST_BINS := tests/test_lexer tests/test_parser

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@

# Rebuild an object file when its .cpp or any header changes.
%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Unit tests for the lexer and parser (pure functions, no shell needed).
test: $(TEST_BINS)
	./tests/test_lexer
	./tests/test_parser

tests/test_lexer: tests/test_lexer.cpp lexer.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -I. tests/test_lexer.cpp lexer.cpp -o $@

tests/test_parser: tests/test_parser.cpp lexer.cpp parser.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -I. tests/test_parser.cpp lexer.cpp parser.cpp -o $@

clean:
	rm -f $(OBJS) $(TARGET) $(TEST_BINS)

.PHONY: all test clean
