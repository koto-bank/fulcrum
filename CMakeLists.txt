cmake_minimum_required(VERSION 3.5)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(fulcrum CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS FALSE)

set(FULCRUM_LIB libfulcrum)
set(FULCRUM_COMPILER fulcrum)

## First, build LLVM and Clang

set(LLVM_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/llvm-project/ CACHE STRING "llvm-project root dir")
set(LLVM_BUILD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/llvm-project/_build CACHE STRING "llvm-project build dir")
set(LLVM_INSTALL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/_llvm_install CACHE STRING "llvm-project install dir")

include(ExternalProject)
file(MAKE_DIRECTORY ${LLVM_BUILD_DIR})
ExternalProject_Add(llvm-project
    PREFIX llvm-project
    SOURCE_DIR ${LLVM_ROOT_DIR}/llvm
    BINARY_DIR ${LLVM_BUILD_DIR}
    CMAKE_ARGS
    -DBUILD_SHARED_LIBS=ON
    -DCMAKE_BUILD_TYPE=$<IF:$<CONFIG:Debug>,Debug,Release>
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    -DCMAKE_INSTALL_PREFIX=${LLVM_INSTALL_DIR}
    -DLLVM_ENABLE_ASSERTIONS=ON
    -DLLVM_ENABLE_PIC=ON
    -DLLVM_ENABLE_RTTI=ON
    -DLLVM_TARGETS_TO_BUILD=host
    -DLLVM_ENABLE_PROJECTS=clang
)

### Library

add_library(${FULCRUM_LIB} OBJECT
  "src/ast_nodes.cpp"
  "src/ast_type_storage.cpp"
  "src/c_header_parser.cpp"
  "src/c_module.cpp"
  "src/compiler.cpp"
  "src/codegen_context.cpp"
  "src/expressions.cpp"
  "src/fulcrum_module.cpp"
  "src/token.cpp"
  "src/lexer.cpp"
  "src/parser.cpp"
  "src/semantic_analyzer.cpp"
  "src/types.cpp"
  )

set(LLVM_DIR ${LLVM_INSTALL_DIR}/lib/cmake/llvm CACHE STRING "llvm home dir" FORCE)
find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

target_compile_definitions(${FULCRUM_LIB} PRIVATE "FC_DEBUG=$<CONFIG:Debug>")
target_include_directories(${FULCRUM_LIB} PUBLIC "src")

target_include_directories(${FULCRUM_LIB} SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS} ${CLANG_DEFINITIONS})

target_compile_definitions(${FULCRUM_LIB} INTERFACE ${LLVM_DEFINITIONS_LIST})
set(LLVM_COMPONENTS support core irreader native)
execute_process(COMMAND ${LLVM_CONFIG_BIN} --libs ${LLVM_COMPONENTS} OUTPUT_VARIABLE llvm_libs)

if("${llvm_libs}" STREQUAL "")
    message(WARNING "${LLVM_CONFIG_BIN} failed to fetch components, falling back to llvm_map_components_to_libnames")
    llvm_map_components_to_libnames(llvm_libs ${LLVM_COMPONENTS})
else()
    string(STRIP ${llvm_libs} llvm_libs)
    execute_process(COMMAND ${LLVM_CONFIG_BIN} --libdir OUTPUT_VARIABLE llvm_libdir)
    string(STRIP ${llvm_libdir} llvm_libdir)
    target_link_directories(${FULCRUM_LIB} PUBLIC ${llvm_libdir})
endif()

add_subdirectory(deps/fmt)

option(ARGS_BUILD_EXAMPLE "" OFF)
option(ARGS_BUILD_UNITTESTS "" OFF)
add_subdirectory(deps/args)
target_include_directories(${FULCRUM_LIB} SYSTEM PUBLIC "deps/args")

add_subdirectory(deps/spdlog)

target_compile_options(${FULCRUM_LIB} PRIVATE -Wall -Wextra -Wpedantic -Werror -Wsuggest-override)
target_link_libraries(${FULCRUM_LIB} PRIVATE args clang-cpp spdlog::spdlog ${llvm_libs})
target_link_libraries(${FULCRUM_LIB} PUBLIC fmt::fmt)

### Compiler

add_executable(${FULCRUM_COMPILER} "src/main.cpp" $<TARGET_OBJECTS:${FULCRUM_LIB}>)
target_compile_definitions(${FULCRUM_COMPILER} PRIVATE "FC_DEBUG=$<CONFIG:Debug>")
target_link_libraries(${FULCRUM_COMPILER} ${FULCRUM_LIB} spdlog::spdlog)
target_compile_options(${FULCRUM_COMPILER} PRIVATE -Wall -Wextra -Wpedantic -Werror -Wsuggest-override)

### Tests

set(FULCRUM_TESTS fulcrum-tests)
file(GLOB_RECURSE TEST_SRCS "src/*.test.cpp")
add_subdirectory(deps/googletest googletest)
add_executable(${FULCRUM_TESTS}
    "tests/main.cpp"
    "src/tree_matcher.cpp"
    ${TEST_SRCS}
    $<TARGET_OBJECTS:libfulcrum>)

target_compile_definitions(${FULCRUM_TESTS} PRIVATE "FC_DEBUG=$<CONFIG:Debug>")
target_link_libraries(${FULCRUM_TESTS} libfulcrum gtest)
