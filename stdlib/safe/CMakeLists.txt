cmake_minimum_required(VERSION 3.16)
project(cplus_safecrt C)
set(CMAKE_C_STANDARD 99)

add_library(cplus_safecrt STATIC
  src/safe_alloc.c
  src/safe_diag.c
  src/safe_error.c
  src/safe_format.c
  src/safe_io.c
  src/safe_log.c
  src/safe_stats.c
  src/safe_memory.c
  src/safe_platform.c
  src/safe_runtime.c
  src/safe_string.c
)
target_include_directories(cplus_safecrt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/safeclib/include)
