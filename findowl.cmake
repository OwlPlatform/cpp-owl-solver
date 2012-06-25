# - Try to find libowl and define:
#  OWL_FOUND
#  OWL_INCLUDE_DIRS
#  OWL_LIBRARIES

include(LibFindMacros)

# Dependencies
libfind_package(Owl owl)

# Use pkg-config to get hints about paths
libfind_pkg_check_modules(OWL_PKGCONF owl)

# Include dir
find_path(OWL_INCLUDE_DIR
  NAMES world_model_protocol.hpp
  PATHS ${OWL_PKGCONF_INCLUDE_DIRS}
)

# Finally the library itself
find_library(OWL_LIBRARY
  NAMES Owl
  PATHS ${OWL_PKGCONF_LIBRARY_DIRS}
)

# Set directory variables and give them to libfind

# Set the include dir variables and the libraries and let libfind_process do the rest.
set(OWL_PROCESS_INCLUDES OWL_INCLUDE_DIR OWL_INCLUDE_DIRS)
set(OWL_PROCESS_LIBS OWL_LIBRARY OWL_LIBRARIES)
libfind_process(OWL)
