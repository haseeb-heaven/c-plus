cmake_minimum_required(VERSION 3.16)
project(cplus_safestringlib C)
set(CMAKE_C_STANDARD 99)

add_library(cplus_safestringlib STATIC
  safeclib/abort_handler_s.c
  safeclib/ignore_handler_s.c
  safeclib/mem_primitives_lib.c
  safeclib/memcmp_s.c
  safeclib/memcpy_s.c
  safeclib/memmove_s.c
  safeclib/memset_s.c
  safeclib/memzero_s.c
  safeclib/safe_mem_constraint.c
  safeclib/safe_str_constraint.c
  safeclib/strcat_s.c
  safeclib/strcpy_s.c
  safeclib/strncat_s.c
  safeclib/strncpy_s.c
  safeclib/strnlen_s.c
  safeclib/strcmp_s.c
)
target_include_directories(cplus_safestringlib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/safeclib
)
# Force include rename map for all compiled upstream files.
target_compile_options(cplus_safestringlib PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/rename_symbols.h)
target_compile_definitions(cplus_safestringlib PRIVATE __STDC_WANT_LIB_EXT1__=0)
