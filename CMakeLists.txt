cmake_minimum_required(VERSION 3.25)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(fulcrum VERSION 0.1.0 LANGUAGES CXX C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS FALSE)

set(FULCRUM_LIB libfulcrum)
set(FULCRUM_COMPILER fulcrum)

if ("${LLVM_CONFIG_BIN}" STREQUAL "")
    set(LLVM_CONFIG_BIN "llvm-config")
endif()

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
target_compile_definitions(${FULCRUM_LIB} PRIVATE "FC_DEBUG=$<CONFIG:Debug>")
target_include_directories(${FULCRUM_LIB} PUBLIC "src")

configure_file("src/fulcrum_version.hpp.in"
               "${CMAKE_CURRENT_BINARY_DIR}/generated/fulcrum_version.hpp" @ONLY)
target_include_directories(${FULCRUM_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

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

set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/fmt)

option(ARGS_BUILD_EXAMPLE "" OFF)
option(ARGS_BUILD_UNITTESTS "" OFF)
add_subdirectory(deps/args EXCLUDE_FROM_ALL)
target_include_directories(${FULCRUM_LIB} SYSTEM PUBLIC "deps/args")

set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
add_subdirectory(deps/spdlog)

target_compile_options(${FULCRUM_LIB} PRIVATE -Wall -Wextra -Wpedantic -Werror -Wsuggest-override)
target_link_libraries(${FULCRUM_LIB} PRIVATE args clang-cpp spdlog::spdlog ${llvm_libs})
target_link_libraries(${FULCRUM_LIB} PUBLIC fmt::fmt)

### Compiler

add_executable(${FULCRUM_COMPILER} "src/main.cpp" $<TARGET_OBJECTS:${FULCRUM_LIB}>)
target_compile_definitions(${FULCRUM_COMPILER} PRIVATE "FC_DEBUG=$<CONFIG:Debug>")
target_link_libraries(${FULCRUM_COMPILER} PRIVATE ${FULCRUM_LIB} spdlog::spdlog)
target_compile_options(${FULCRUM_COMPILER} PRIVATE -Wall -Wextra -Wpedantic -Werror -Wsuggest-override)

### Install and export

set(FULCRUM_INSTALL_CMAKEDIR "${CMAKE_INSTALL_DATADIR}/cmake/Fulcrum")
set(FULCRUM_INSTALL_MODULEDIR "${FULCRUM_INSTALL_CMAKEDIR}/modules")

set(FULCRUM_LANGUAGE_MODULES
  "cmake/CMakeDetermineFulcrumCompiler.cmake"
  "cmake/CMakeFulcrumCompiler.cmake.in"
  "cmake/CMakeTestFulcrumCompiler.cmake"
  "cmake/CMakeFulcrumInformation.cmake"
  )

set_target_properties(${FULCRUM_COMPILER} PROPERTIES EXPORT_NAME compiler)

install(TARGETS ${FULCRUM_COMPILER}
        EXPORT FulcrumTargets
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

install(EXPORT FulcrumTargets
        NAMESPACE Fulcrum::
        FILE FulcrumTargets.cmake
        DESTINATION "${FULCRUM_INSTALL_CMAKEDIR}")

install(FILES ${FULCRUM_LANGUAGE_MODULES}
        DESTINATION "${FULCRUM_INSTALL_MODULEDIR}")

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/FulcrumConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMinorVersion
  ARCH_INDEPENDENT)

# Config for the install tree
set(FULCRUM_CONFIG_MODULEDIR "${FULCRUM_INSTALL_MODULEDIR}")
configure_package_config_file(
  "cmake/FulcrumConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/install-config/FulcrumConfig.cmake"
  INSTALL_DESTINATION "${FULCRUM_INSTALL_CMAKEDIR}"
  PATH_VARS FULCRUM_CONFIG_MODULEDIR)

install(FILES
          "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/install-config/FulcrumConfig.cmake"
          "${CMAKE_CURRENT_BINARY_DIR}/FulcrumConfigVersion.cmake"
        DESTINATION "${FULCRUM_INSTALL_CMAKEDIR}")

# Config for the build tree, so examples/ and ctest can use
# find_package(Fulcrum) with -DFulcrum_ROOT=<build dir> and no install step.
export(EXPORT FulcrumTargets
       NAMESPACE Fulcrum::
       FILE "${CMAKE_CURRENT_BINARY_DIR}/FulcrumTargets.cmake")

set(FULCRUM_CONFIG_MODULEDIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
configure_package_config_file(
  "cmake/FulcrumConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/FulcrumConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_CURRENT_BINARY_DIR}"
  INSTALL_PREFIX "${CMAKE_CURRENT_BINARY_DIR}"
  PATH_VARS FULCRUM_CONFIG_MODULEDIR)

### Tests

set(FULCRUM_TESTS fulcrum-tests)
file(GLOB_RECURSE TEST_SRCS "src/*.test.cpp")
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/googletest googletest)
add_executable(${FULCRUM_TESTS}
    "tests/main.cpp"
    "src/tree_matcher.cpp"
    ${TEST_SRCS}
    $<TARGET_OBJECTS:libfulcrum>)

target_compile_definitions(${FULCRUM_TESTS} PRIVATE "FC_DEBUG=$<CONFIG:Debug>")
target_link_libraries(${FULCRUM_TESTS} libfulcrum gtest)

enable_testing()
add_test(NAME fulcrum-unit-tests COMMAND ${FULCRUM_TESTS})

# End-to-end check of the Fulcrum language modules: configure, build and run
# examples/hello against this build tree
add_test(NAME fulcrum-examples
  COMMAND "${CMAKE_CTEST_COMMAND}"
    --build-and-test
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/hello"
      "${CMAKE_CURRENT_BINARY_DIR}/examples/hello"
    --build-generator "${CMAKE_GENERATOR}"
    --build-makeprogram "${CMAKE_MAKE_PROGRAM}"
    --build-options
      "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
      "-DFulcrum_ROOT=${CMAKE_CURRENT_BINARY_DIR}"
    --test-command "${CMAKE_CTEST_COMMAND}" --output-on-failure)
set_tests_properties(fulcrum-examples PROPERTIES TIMEOUT 600)
