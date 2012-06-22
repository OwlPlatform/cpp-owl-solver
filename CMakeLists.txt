cmake_minimum_required (VERSION 2.6)
project (LibOwlSolverC++)

# The version number.
set (LibOwlSolver_VERSION_MAJOR 1)
set (LibOwlSolver_VERSION_MINOR 0)
set (LibOwlSolver_VERSION_REVISION 0)

set(CMAKE_CXX_FLAGS "-O2 -std=c++0x -Wall -Wextra -Wno-sign-compare")

#SUBDIRS(src)
#add_subdirectory ("${PROJECT_SOURCE_DIR}/src")

include_directories ("${LibOwlSolverC++_SOURCE_DIR}/include")
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

