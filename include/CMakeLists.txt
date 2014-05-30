SET(IncludeFiles
  client_world_connection.hpp
  solver_world_connection.hpp
  solver_aggregator_connection.hpp
)

#Need to install all of the include files
SET(INCLUDE_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/include/owl/" CACHE PATH "The subdirectory to the header prefix")
INSTALL(FILES ${IncludeFiles} PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ DESTINATION ${INCLUDE_INSTALL_DIR})

