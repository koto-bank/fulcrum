cmake_minimum_required(VERSION 3.25)

# C must come first: the Fulcrum compiler cannot link, so CMake drives every
# Fulcrum link step through the C compiler driver.
project(fulcrum-hello LANGUAGES C)

find_package(Fulcrum REQUIRED)
enable_language(Fulcrum)

enable_testing()

# 1. A pure-Fulcrum executable
add_executable(hello hello.fc)

add_test(NAME hello COMMAND hello)
set_tests_properties(hello PROPERTIES PASS_REGULAR_EXPRESSION "Hello, world!")

# 2. Fulcrum static and shared libraries, consumed from C
add_library(greet-static STATIC greet.fc)

add_library(greet-shared SHARED greet.fc)
set_target_properties(greet-shared PROPERTIES OUTPUT_NAME greet SOVERSION 0)

# 3. Mixed targets
add_executable(use-greet-static main.c)
target_link_libraries(use-greet-static PRIVATE greet-static)

add_test(NAME use-greet-static COMMAND use-greet-static)
set_tests_properties(use-greet-static PROPERTIES PASS_REGULAR_EXPRESSION "Hello from a Fulcrum library")

add_executable(use-greet-shared main.c)
target_link_libraries(use-greet-shared PRIVATE greet-shared)

add_test(NAME use-greet-shared COMMAND use-greet-shared)
set_tests_properties(use-greet-shared PROPERTIES PASS_REGULAR_EXPRESSION "Hello from a Fulcrum library")
