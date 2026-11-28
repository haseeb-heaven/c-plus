add_library(cplus_hardened_libc STATIC IMPORTED)
set_target_properties(cplus_hardened_libc PROPERTIES
  IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/../../../lib/libcplus_hardened_libc.a"
  INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../../include/cplus-libc")
