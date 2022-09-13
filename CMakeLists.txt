cmake_minimum_required(VERSION 3.16)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(fulcrum CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS FALSE)

find_package(LLVM REQUIRED)
find_package(Clang REQUIRED)

add_executable(fulcrum
  "src/main.cpp"
  "src/codegen_context.cpp"
  "src/expressions.cpp"
  "src/types.cpp")
target_include_directories(fulcrum PUBLIC "src")

target_include_directories(fulcrum SYSTEM PUBLIC
  ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND
  ${LLVM_DEFINITIONS} ${CLANG_DEFINITIONS})
target_compile_definitions(fulcrum PUBLIC ${LLVM_DEFINITIONS_LIST})

llvm_map_components_to_libnames(llvm_libs support)

target_link_libraries(fulcrum LLVM libclang clang-cpp ${llvm_libs})

set(GRAMMAR_DIR "${CMAKE_BINARY_DIR}/grammar")
file(MAKE_DIRECTORY ${GRAMMAR_DIR})
add_custom_command(
  OUTPUT "${GRAMMAR_DIR}/generated_parser.cpp"
  DEPENDS ${CMAKE_SOURCE_DIR}/src/grammar.leg
  COMMAND leg -o ${GRAMMAR_DIR}/generated_parser.cpp ${CMAKE_SOURCE_DIR}/src/grammar.leg
)

target_sources(fulcrum PUBLIC "${GRAMMAR_DIR}/generated_parser.cpp")

add_subdirectory(deps/fmt)
target_link_libraries(fulcrum fmt)

option(ARGS_BUILD_EXAMPLE "" OFF)
option(ARGS_BUILD_UNITTESTS "" OFF)
add_subdirectory(deps/args)
target_include_directories(fulcrum SYSTEM PUBLIC "deps/args")
target_link_libraries(fulcrum args)

target_compile_options(fulcrum PRIVATE -Wall -Wextra -Wpedantic -Werror -Wsuggest-override)
