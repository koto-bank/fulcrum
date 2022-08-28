cmake_minimum_required(VERSION 3.16)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(lisp_project CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS FALSE)

find_package(LLVM REQUIRED)
find_package(Clang REQUIRED)

add_executable(lisp_project
  "src/main.cpp")

target_include_directories(lisp_project PUBLIC
  ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND
  ${LLVM_DEFINITIONS} ${CLANG_DEFINITIONS})
target_compile_definitions(lisp_project PUBLIC ${LLVM_DEFINITIONS_LIST})

llvm_map_components_to_libnames(llvm_libs support)

target_link_libraries(lisp_project LLVM libclang ${llvm_libs})
