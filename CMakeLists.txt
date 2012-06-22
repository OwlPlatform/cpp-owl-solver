cmake_minimum_required (VERSION 2.6)
project (LibOwlSolverC++)

# The version number.
set (LibOwlSolver_VERSION_MAJOR 1)
set (LibOwlSolver_VERSION_MINOR 0)
set (LibOwlSolver_VERSION_REVISION 0)

# Adding the link command to libowl in this line.
# Should technically run a bunch of cmake scripts to discover where libowl
# was installed.
set(CMAKE_CXX_FLAGS "-O2 -std=c++0x -Wall -Wextra -Wno-sign-compare -lowl")

FIND_PATH(OWL_INC_DIR NAMES owl PATHS /usr/include /usr/local/include)
IF(NOT OWL_INC_DIR)
  MESSAGE(FATAL_ERROR "Unable to find libOwl installation!")
ELSE(NOT OWL_INC_DIR)
  #Truncate the search path
  STRING(REGEX REPLACE "/owl" "" OWL_INC_DIR ${OWL_INC_DIR})
ENDIF(NOT OWL_INC_DIR)

#SUBDIRS(src)
#add_subdirectory ("${PROJECT_SOURCE_DIR}/src")

#include(findowl.cmake)

include_directories ("${LibOwlSolverC++_SOURCE_DIR}/include" "${OWL_INC_DIR}")

add_subdirectory (src)
add_subdirectory (include)

## the following are directories where stuff will be installed to
SET (LIB_SUFFIX "" CACHE STRING "Define suffix of directory name (32/64)" )
SET(PKGCONFIG_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}/lib${LIB_SUFFIX}/pkgconfig/" CACHE STRING "Base directory for pkgconfig files")
SET(INCLUDE_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/include/owl/" CACHE PATH "The subdirectory to the header prefix")

CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/owl.pc.cmake ${CMAKE_CURRENT_BINARY_DIR}/owl.pc @ONLY)
INSTALL( FILES ${CMAKE_CURRENT_BINARY_DIR}/owl.pc DESTINATION ${PKGCONFIG_INSTALL_PREFIX})

# uninstall target
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)

