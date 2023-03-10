cmake_minimum_required(VERSION 3.16)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
set(FULCRUM_TESTS fulcrum-tests)

project(${FULCRUM_TESTS} CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS FALSE)

file(GLOB_RECURSE TEST_SRCS "../src/*.test.cpp")

add_subdirectory(../ fulcrum)
add_subdirectory(../deps/googletest googletest)

add_executable(${FULCRUM_TESTS}
    "main.cpp"
    ${TEST_SRCS}
    $<TARGET_OBJECTS:libfulcrum>)
target_link_libraries(${FULCRUM_TESTS} libfulcrum gtest)
