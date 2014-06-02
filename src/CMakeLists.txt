SET(SourceFiles
  client_world_connection.cpp
  solver_world_connection.cpp
  solver_aggregator_connection.cpp
)

add_library (owl-solver SHARED ${SourceFiles})
target_link_libraries (owl-solver owl-common)
set (FULLVERSION ${LibOwlSolver_VERSION_MAJOR}.${LibOwlSolver_VERSION_MINOR}.${LibOwlSolver_VERSION_REVISION})
SET_TARGET_PROPERTIES(owl-solver PROPERTIES VERSION ${FULLVERSION} SOVERSION ${FULLVERSION})

#Set the correct library directory suffix
if(NOT DEFINED LIB_SUFFIX)
  get_property(LIB64 GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS)
  if (${LIB64} STREQUAL "TRUE")
    set(LIBSUFFIX 64)
  else()
    set(LIBSUFFIX "")
  endif()
  set(LIB_SUFFIX ${LIBSUFFIX} CACHE STRING "lib directory suffix")
endif()
SET(CMAKE_INSTALL_LIBDIR lib${LIB_SUFFIX} CACHE PATH "Output directory for libraries")
INSTALL(TARGETS owl-solver DESTINATION ${CMAKE_INSTALL_LIBDIR})
