# NexusShell: CMake build (the Makefile is the primary build; this is an equivalent).
#
#   cmake -S . -B build && cmake --build build
#   ctest --test-dir build --output-on-failure
cmake_minimum_required(VERSION 3.10)
project(NexusShell CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

add_library(nexus_core STATIC
    builtins.cpp lexer.cpp parser.cpp redirect.cpp executor.cpp
    jobs.cpp signals.cpp history.cpp input.cpp prompt.cpp)
target_include_directories(nexus_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(nexusshell main.cpp)
target_link_libraries(nexusshell nexus_core)

enable_testing()

# Unit tests: pure functions, no terminal needed.
foreach(name lexer parser jobs history prompt)
    add_executable(test_${name} tests/test_${name}.cpp)
    target_link_libraries(test_${name} nexus_core)
    add_test(NAME ${name} COMMAND test_${name})
endforeach()

# End-to-end tests drive the real binary.
find_package(Python3 COMPONENTS Interpreter)
add_test(NAME smoke
         COMMAND bash ${CMAKE_SOURCE_DIR}/tests/smoke.sh $<TARGET_FILE:nexusshell>)
if(Python3_FOUND)
    foreach(name job_control line_editing ui)
        add_test(NAME pty_${name} COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tests/test_${name}.py)
        set_tests_properties(pty_${name} PROPERTIES
                 ENVIRONMENT NEXUSSHELL_BIN=$<TARGET_FILE:nexusshell>)
    endforeach()
endif()
