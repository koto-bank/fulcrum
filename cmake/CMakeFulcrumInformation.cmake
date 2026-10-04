# Rule variables for the Fulcrum language.
#
# Fulcrum compiles one module into one native relocatable object and cannot
# link, so every link step here shells out to the C compiler driver.

include(CMakeLanguageInformation)

if(NOT CMAKE_C_COMPILER_LOADED)
  message(FATAL_ERROR
    "The Fulcrum language requires the C language to be enabled: the Fulcrum "
    "compiler emits relocatable objects only, so CMake must use the C compiler "
    "driver for every Fulcrum link step.\n"
    "Add C to your project() call, e.g. project(myapp C Fulcrum), or call "
    "enable_language(C) before enable_language(Fulcrum).")
endif()

set(CMAKE_Fulcrum_OUTPUT_EXTENSION .o)

# --- PIC / PIE -------------------------------------------------------------
# fulcrum has exactly one relocation-model switch, --pic, which selects
# llvm::Reloc::Model::PIC_. There is no way to ask for the static model back,
# and the host C driver defaults to -pie on modern distros, so a non-PIC
# Fulcrum object cannot be linked into an executable at all. Hence --pic is
# baked into CMAKE_Fulcrum_COMPILE_OBJECT below; these entries make an explicit
# POSITION_INDEPENDENT_CODE request a harmless no-op rather than an error
# (args::Flag tolerates a repeated --pic).
set(CMAKE_Fulcrum_COMPILE_OPTIONS_PIC "--pic")
set(CMAKE_Fulcrum_COMPILE_OPTIONS_PIE "--pic")
set(CMAKE_Fulcrum_COMPILE_OPTIONS_DLL "")
set(CMAKE_Fulcrum_LINK_OPTIONS_PIE    "-pie")
set(CMAKE_Fulcrum_LINK_OPTIONS_NO_PIE "-no-pie")
set(CMAKE_Fulcrum_LINK_PIE_SUPPORTED    TRUE)
set(CMAKE_Fulcrum_LINK_NO_PIE_SUPPORTED TRUE)

# These must be empty, and must be set before the platform-flag macro below:
# the C values ("-fPIC") land on the *compile* line of every source in a
# SHARED target, and fulcrum's argument parser would reject them.
set(CMAKE_SHARED_LIBRARY_Fulcrum_FLAGS "")
set(CMAKE_SHARED_MODULE_Fulcrum_FLAGS "")

set(CMAKE_INCLUDE_FLAG_Fulcrum "-I")

# The link rules invoke the C driver, so LINKER: must expand GNU-style.
set(CMAKE_Fulcrum_LINKER_WRAPPER_FLAG "-Wl,")
set(CMAKE_Fulcrum_LINKER_WRAPPER_FLAG_SEP ",")

# Inherit the platform's -shared / -Wl,-soname, / -Wl,-rpath, / -Wl,-Bstatic
# machinery from the C toolchain. Skip this and shared libraries lose -shared,
# their soname and their rpath all at once. Every assignment inside is guarded
# by if(NOT DEFINED), so the overrides above survive.
include(CMakeCommonLanguageInclude)
_cmake_common_language_platform_flags(Fulcrum)

# (import :c "stdio.h") is resolved by fulcrum's own -I search (tryToFindImport
# in src/compiler.cpp), not by the system compiler's built-in search paths, so
# the system include directories have to be passed explicitly or every C import
# fails with "Failed to find C include 'stdio.h'". Reuse what CMake already
# discovered for C -- this is a second reason the C language is required.
if(NOT CMAKE_Fulcrum_STANDARD_INCLUDE_DIRECTORIES)
  set(CMAKE_Fulcrum_STANDARD_INCLUDE_DIRECTORIES
      ${CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES})
endif()

if(CMAKE_USER_MAKE_RULES_OVERRIDE_Fulcrum)
  include(${CMAKE_USER_MAKE_RULES_OVERRIDE_Fulcrum} RESULT_VARIABLE _override)
  set(CMAKE_USER_MAKE_RULES_OVERRIDE_Fulcrum "${_override}")
endif()

# --- per-config flags ------------------------------------------------------
# fulcrum has no -O<n>, no -g and no -DNDEBUG, so every per-config default is
# empty. In particular Fulcrum must not inherit C's "-O3 -DNDEBUG".
set(CMAKE_Fulcrum_FLAGS_INIT "$ENV{FULCRUMFLAGS} ${CMAKE_Fulcrum_FLAGS_INIT}")
set(CMAKE_Fulcrum_FLAGS_DEBUG_INIT "")
set(CMAKE_Fulcrum_FLAGS_RELEASE_INIT "")
set(CMAKE_Fulcrum_FLAGS_MINSIZEREL_INIT "")
set(CMAKE_Fulcrum_FLAGS_RELWITHDEBINFO_INIT "")

cmake_initialize_per_config_variable(CMAKE_Fulcrum_FLAGS
                                     "Flags used by the Fulcrum compiler")

# --- rules -----------------------------------------------------------------
# <DEFINES> is deliberately absent: fulcrum has no -D switch and would treat
# "-DFOO" as an unrecognised flag. COMPILE_DEFINITIONS on a Fulcrum source are
# therefore silently ignored; see README.org.
if(NOT CMAKE_Fulcrum_COMPILE_OBJECT)
  set(CMAKE_Fulcrum_COMPILE_OBJECT
    "<CMAKE_Fulcrum_COMPILER> --pic <INCLUDES> <FLAGS> -o <OBJECT> <SOURCE>")
endif()

# Incremental archiving, so a large object count cannot overflow the command
# line. CMAKE_Fulcrum_CREATE_STATIC_LIBRARY, if a user sets one, wins over these.
if(NOT DEFINED CMAKE_Fulcrum_ARCHIVE_CREATE)
  set(CMAKE_Fulcrum_ARCHIVE_CREATE "<CMAKE_AR> qc <TARGET> <LINK_FLAGS> <OBJECTS>")
endif()
if(NOT DEFINED CMAKE_Fulcrum_ARCHIVE_APPEND)
  set(CMAKE_Fulcrum_ARCHIVE_APPEND "<CMAKE_AR> q <TARGET> <LINK_FLAGS> <OBJECTS>")
endif()
if(NOT DEFINED CMAKE_Fulcrum_ARCHIVE_FINISH)
  set(CMAKE_Fulcrum_ARCHIVE_FINISH "<CMAKE_RANLIB> <TARGET>")
endif()

# The C driver is baked in at configure time, the way CMakeRustInformation.cmake
# bakes in ${CMAKE_Rust_COMPILER}. <FLAGS> and <LANGUAGE_COMPILE_FLAGS> are
# intentionally absent from the link rules: on a link line they expand to
# CMAKE_Fulcrum_FLAGS, i.e. fulcrum switches the C driver would reject.
# <LINK_FLAGS> already carries -shared, the soname flag, the rpath flags and
# the PIE options, all supplied by _cmake_common_language_platform_flags above.
if(NOT CMAKE_Fulcrum_CREATE_SHARED_LIBRARY)
  set(CMAKE_Fulcrum_CREATE_SHARED_LIBRARY
    "\"${CMAKE_C_COMPILER}\" <LINK_FLAGS> <SONAME_FLAG><TARGET_SONAME> -o <TARGET> <OBJECTS> <LINK_LIBRARIES>")
endif()

if(NOT CMAKE_Fulcrum_CREATE_SHARED_MODULE)
  set(CMAKE_Fulcrum_CREATE_SHARED_MODULE "${CMAKE_Fulcrum_CREATE_SHARED_LIBRARY}")
endif()

if(NOT CMAKE_Fulcrum_LINK_EXECUTABLE)
  set(CMAKE_Fulcrum_LINK_EXECUTABLE
    "\"${CMAKE_C_COMPILER}\" <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
endif()

# We ship no Internal/CMakeFulcrumLinkerInformation.cmake and never invoke a
# bare linker, so opt out of CMake's linker-information machinery. Consequence:
# LINKER_TYPE and LINK_WHAT_YOU_USE are inert on Fulcrum-linked targets.
set(CMAKE_Fulcrum_USE_LINKER_INFORMATION FALSE)

set(CMAKE_Fulcrum_INFORMATION_LOADED 1)
