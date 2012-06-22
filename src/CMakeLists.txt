SET(SourceFiles
  client_world_connection.cpp
  solver_world_connection.cpp
)

add_library (Owl SHARED ${SourceFiles})
set (FULLVERSION ${LibOwlSolver_VERSION_MAJOR}.${LibOwlSolver_VERSION_MINOR}.${LibOwlSolver_VERSION_REVISION})
SET_TARGET_PROPERTIES(Owl PROPERTIES VERSION ${FULLVERSION} SOVERSION ${FULLVERSION})

SET(CMAKE_INSTALL_LIBDIR lib CACHE PATH "Output directory for libraries")
INSTALL(TARGETS Owl DESTINATION ${CMAKE_INSTALL_LIBDIR})
