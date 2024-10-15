cmake_minimum_required(VERSION 3.5)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(fulcrum CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS FALSE)

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
  "src/ast_nodes.cpp"
  "src/semantic_analyzer.cpp"
  "src/tree_matcher.cpp"
  "src/types.cpp"
  "src/c_header_parser.cpp")
target_compile_definitions(${FULCRUM_LIB} PRIVATE "FC_DEBUG=$<CONFIG:Debug>")
target_include_directories(${FULCRUM_LIB} PUBLIC "src")


find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

target_include_directories(${FULCRUM_LIB} SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS} ${CLANG_DEFINITIONS})

target_compile_definitions(${FULCRUM_LIB} INTERFACE ${LLVM_DEFINITIONS_LIST})
set(LLVM_COMPONENTS support core irreader native)
execute_process(COMMAND llvm-config --libs ${LLVM_COMPONENTS} OUTPUT_VARIABLE llvm_libs)

if("${llvm_libs}" STREQUAL "")
    llvm_map_components_to_libnames(llvm_libs ${LLVM_COMPONENTS})
else()
    string(STRIP ${llvm_libs} llvm_libs)
    execute_process(COMMAND llvm-config --libdir OUTPUT_VARIABLE llvm_libdir)
    string(STRIP ${llvm_libdir} llvm_libdir)
    target_link_directories(${FULCRUM_LIB} PUBLIC ${llvm_libdir})
endif()

add_subdirectory(deps/fmt)

option(ARGS_BUILD_EXAMPLE "" OFF)
option(ARGS_BUILD_UNITTESTS "" OFF)
add_subdirectory(deps/args)
target_include_directories(${FULCRUM_LIB} SYSTEM PUBLIC "deps/args")

target_compile_options(${FULCRUM_LIB} PRIVATE -Wall -Wextra -Wpedantic -Werror -Wsuggest-override)
target_link_libraries(${FULCRUM_LIB} PRIVATE fmt args clang-cpp ${llvm_libs})

### Compiler

add_executable(${FULCRUM_COMPILER} "src/main.cpp" $<TARGET_OBJECTS:${FULCRUM_LIB}>)
target_compile_definitions(${FULCRUM_COMPILER} PRIVATE "FC_DEBUG=$<CONFIG:Debug>")
target_link_libraries(${FULCRUM_COMPILER} ${FULCRUM_LIB})
target_compile_options(${FULCRUM_COMPILER} PRIVATE -Wall -Wextra -Wpedantic -Werror -Wsuggest-override)
