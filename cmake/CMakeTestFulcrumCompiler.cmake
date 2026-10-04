# Confirm the Fulcrum compiler is usable. Deliberately no try_compile here.
# 
# CMakeDetermineFulcrumCompiler.cmake has already run "fulcrum --version" and
# failed hard if that did not work, which is most of what a try_compile of a
# trivial module would add. The rest is covered better by the project's own
# "fulcrum-examples" test, which compiles, archives, links and runs real
# Fulcrum programs and reports failures legibly.

if(CMAKE_Fulcrum_COMPILER_FORCED)
  # The compiler configuration was forced by the user.
  # Assume the user has configured all compiler information.
  set(CMAKE_Fulcrum_COMPILER_WORKS TRUE)
  return()
endif()

configure_file(
  "${CMAKE_CURRENT_LIST_DIR}/CMakeFulcrumCompiler.cmake.in"
  "${CMAKE_PLATFORM_INFO_DIR}/CMakeFulcrumCompiler.cmake"
  @ONLY)

include("${CMAKE_PLATFORM_INFO_DIR}/CMakeFulcrumCompiler.cmake")
