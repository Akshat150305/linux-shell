CXX      := g++
CXXFLAGS := -Wall -Wextra -std=c++17 -g
TARGET   := nexusshell
SRCS     := main.cpp builtins.cpp
OBJS     := $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@

# Rebuild an object file when its .cpp or the shared header changes.
%.o: %.cpp builtins.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
