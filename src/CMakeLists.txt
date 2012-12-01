SET(SourceFiles
  client_world_connection.cpp
  solver_world_connection.cpp
  solver_aggregator_connection.cpp
)

add_library (owl-solver SHARED ${SourceFiles})
target_link_libraries (owl-solver owl-common)
set (FULLVERSION ${LibOwlSolver_VERSION_MAJOR}.${LibOwlSolver_VERSION_MINOR}.${LibOwlSolver_VERSION_REVISION})
SET_TARGET_PROPERTIES(owl-solver PROPERTIES VERSION ${FULLVERSION} SOVERSION ${FULLVERSION})

SET(CMAKE_INSTALL_LIBDIR lib CACHE PATH "Output directory for libraries")
INSTALL(TARGETS owl-solver DESTINATION ${CMAKE_INSTALL_LIBDIR})
