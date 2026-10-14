CXX      := g++
CXXFLAGS := -Wall -Wextra -std=c++17 -g
TARGET   := nexusshell
SRCS     := main.cpp builtins.cpp lexer.cpp
OBJS     := $(SRCS:.cpp=.o)
HEADERS  := builtins.h lexer.h
TEST_BIN := tests/test_lexer

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@

# Rebuild an object file when its .cpp or any header changes.
%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Unit tests for the lexer (pure function, no shell needed).
test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): tests/test_lexer.cpp lexer.cpp lexer.h
	$(CXX) $(CXXFLAGS) -I. tests/test_lexer.cpp lexer.cpp -o $@

clean:
	rm -f $(OBJS) $(TARGET) $(TEST_BIN)

.PHONY: all test clean
