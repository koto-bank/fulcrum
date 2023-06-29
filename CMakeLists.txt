cmake_minimum_required(VERSION 3.16)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(fulcrum CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS FALSE)

find_package(LLVM REQUIRED)
find_package(Clang REQUIRED)

set(FULCRUM_LIB libfulcrum)
set(FULCRUM_COMPILER fulcrum)

### Library

add_library(${FULCRUM_LIB} OBJECT
  "src/compiler.cpp"
  "src/codegen_context.cpp"
  "src/expressions.cpp"
  "src/parser.cpp"
  "src/token.cpp"
  "src/lexer.cpp"
  "src/parse_context.cpp"
  "src/tree_matcher.cpp"
  "src/types.cpp")
target_compile_definitions(${FULCRUM_LIB} PRIVATE "FC_DEBUG=$<CONFIG:Debug>")
target_include_directories(${FULCRUM_LIB} PUBLIC "src")

target_include_directories(${FULCRUM_LIB} SYSTEM PUBLIC
  ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND
  ${LLVM_DEFINITIONS} ${CLANG_DEFINITIONS})
target_compile_definitions(${FULCRUM_LIB} PUBLIC ${LLVM_DEFINITIONS_LIST})

llvm_map_components_to_libnames(llvm_libs support)

set(GRAMMAR_DIR "${CMAKE_BINARY_DIR}/grammar")
file(MAKE_DIRECTORY ${GRAMMAR_DIR})
set(GRAMMAR_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/grammar.leg")
add_custom_command(
  OUTPUT "${GRAMMAR_DIR}/generated_parser.cpp"
  DEPENDS "${GRAMMAR_SRC}"
  COMMAND leg -o "${GRAMMAR_DIR}/generated_parser.cpp" "${GRAMMAR_SRC}"
  VERBATIM
)

add_subdirectory(deps/fmt)

option(ARGS_BUILD_EXAMPLE "" OFF)
option(ARGS_BUILD_UNITTESTS "" OFF)
add_subdirectory(deps/args)

target_include_directories(${FULCRUM_LIB} SYSTEM PUBLIC "deps/args")

target_compile_options(${FULCRUM_LIB} PRIVATE -Wall -Wextra -Wpedantic -Werror -Wsuggest-override)
target_link_libraries(${FULCRUM_LIB} fmt args LLVM libclang clang-cpp ${llvm_libs})

### Compiler

add_executable(${FULCRUM_COMPILER} "src/main.cpp" $<TARGET_OBJECTS:${FULCRUM_LIB}>)
target_compile_definitions(${FULCRUM_COMPILER} PRIVATE "FC_DEBUG=$<CONFIG:Debug>")
target_link_libraries(${FULCRUM_COMPILER} ${FULCRUM_LIB})
#target_include_directories(${FULCRUM_COMPILER} PRIVATE ${FULCRUM_INCLUDE_DIRS})
target_compile_options(${FULCRUM_COMPILER} PRIVATE -Wall -Wextra -Wpedantic -Werror -Wsuggest-override)
