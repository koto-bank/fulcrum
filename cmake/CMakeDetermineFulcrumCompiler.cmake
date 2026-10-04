# Locate the Fulcrum compiler and record what CMake needs to know about the
# language. Modelled on CMake's own CMakeDetermineRustCompiler.cmake and
# CMakeDetermineASM_NASMCompiler.cmake.

# Anchored at both ends on purpose: the unanchored "^(Ninja)|(Unix Makefiles)"
# form used upstream parses as (^Ninja)|(Unix Makefiles), which silently admits
# every generator whose name starts with "Ninja". Ninja Multi-Config genuinely
# works here and is listed explicitly; the IDE generators have no code path for
# a custom language.
if(NOT "${CMAKE_GENERATOR}" MATCHES "^(Ninja|Ninja Multi-Config|Unix Makefiles)$")
  message(FATAL_ERROR
    "The Fulcrum language is not supported by the \"${CMAKE_GENERATOR}\" "
    "generator. Use Ninja, Ninja Multi-Config or Unix Makefiles.")
endif()

include(${CMAKE_ROOT}/Modules/CMakeDetermineCompiler.cmake)

if(NOT CMAKE_Fulcrum_COMPILER)
  # Prefer the FULCRUM_COMPILER environment variable. _cmake_find_compiler only
  # consults CMAKE_Fulcrum_COMPILER_INIT/_LIST/_HINTS, so the env var has to be
  # folded into _INIT here -- the same way CMakeDetermineCCompiler.cmake
  # handles CC.
  if(NOT "$ENV{FULCRUM_COMPILER}" STREQUAL "")
    get_filename_component(CMAKE_Fulcrum_COMPILER_INIT "$ENV{FULCRUM_COMPILER}"
                           PROGRAM PROGRAM_ARGS CMAKE_Fulcrum_FLAGS_ENV_INIT)
    if(CMAKE_Fulcrum_FLAGS_ENV_INIT)
      set(CMAKE_Fulcrum_COMPILER_ARG1 "${CMAKE_Fulcrum_FLAGS_ENV_INIT}"
          CACHE STRING "Arguments to the Fulcrum compiler")
    endif()
    if(NOT EXISTS "${CMAKE_Fulcrum_COMPILER_INIT}")
      message(FATAL_ERROR
        "Could not find the compiler specified in the environment variable "
        "FULCRUM_COMPILER:\n$ENV{FULCRUM_COMPILER}")
    endif()
  endif()

  if(NOT CMAKE_Fulcrum_COMPILER_INIT)
    set(CMAKE_Fulcrum_COMPILER_LIST fulcrum)
  endif()

  _cmake_find_compiler(Fulcrum)
else()
  # Normalises a bare name or a "fulcrum --flag" string into an absolute path
  # plus CMAKE_Fulcrum_COMPILER_ARG1.
  _cmake_find_compiler_path(Fulcrum)
endif()

if(NOT CMAKE_Fulcrum_COMPILER)
  message(FATAL_ERROR
    "Could not find the Fulcrum compiler (\"fulcrum\").\n"
    "Set the cache variable CMAKE_Fulcrum_COMPILER or the FULCRUM_COMPILER "
    "environment variable, or call find_package(Fulcrum) before "
    "enable_language(Fulcrum).")
endif()

mark_as_advanced(CMAKE_Fulcrum_COMPILER)

set(CMAKE_Fulcrum_COMPILER_ID "Fulcrum")
set(CMAKE_Fulcrum_COMPILER_ENV_VAR "FULCRUM_COMPILER")

# The full CMAKE_DETERMINE_COMPILER_ID machinery compiles a preprocessor-based
# probe and inspects the resulting binary's ABI.  Fulcrum has no preprocessor
# and only ever targets the host, so ask it for its version directly instead.
if(CMAKE_Fulcrum_COMPILER)
  execute_process(
    COMMAND "${CMAKE_Fulcrum_COMPILER}" --version
    OUTPUT_VARIABLE _fulcrum_version_output
    ERROR_VARIABLE  _fulcrum_version_error
    RESULT_VARIABLE _fulcrum_version_result
    OUTPUT_STRIP_TRAILING_WHITESPACE
    )
  if(_fulcrum_version_result EQUAL 0 AND
     _fulcrum_version_output MATCHES "fulcrum[ \t]+([0-9]+\\.[0-9]+(\\.[0-9]+)?)")
    set(CMAKE_Fulcrum_COMPILER_VERSION "${CMAKE_MATCH_1}")
  endif()
  unset(_fulcrum_version_output)
  unset(_fulcrum_version_error)
  unset(_fulcrum_version_result)
endif()

set(CMAKE_Fulcrum_SOURCE_FILE_EXTENSIONS fc;fulcrum)

# C headers pulled in with (import :c "...") may legitimately sit in a target's
# source list; they must never be handed to the compiler as translation units.
set(CMAKE_Fulcrum_IGNORE_EXTENSIONS h;H;hpp;HPP;o;O;obj;OBJ;a;A;so;SO;def;DEF)

set(CMAKE_Fulcrum_OUTPUT_EXTENSION .o)

# Higher wins when a target mixes languages.  C is 10 and CXX is 30, so 5 lets
# the C/CXX driver own the link step of a mixed target.  A pure Fulcrum target
# uses CMAKE_Fulcrum_LINK_EXECUTABLE, which also shells out to the C driver, so
# both paths agree.
set(CMAKE_Fulcrum_LINKER_PREFERENCE 5)
set(CMAKE_Fulcrum_LINKER_PREFERENCE_PROPAGATES 0)

configure_file(
  "${CMAKE_CURRENT_LIST_DIR}/CMakeFulcrumCompiler.cmake.in"
  "${CMAKE_PLATFORM_INFO_DIR}/CMakeFulcrumCompiler.cmake"
  @ONLY)
