cmake_minimum_required(VERSION 3.16)
project(cplus_hardened_libc C)
set(CMAKE_C_STANDARD 99)

add_library(cplus_hardened_libc STATIC
  log/log.c
  src/errno/errno.c
  src/stdlib/alloc.c
  src/stdlib/stdlib_extra.c
  src/stdlib/stdlib_extra2.c
  src/string/memory.c
  src/string/string.c
  src/string/extra.c
  src/string/extra2.c
  src/string/extra3.c
  src/stdio/stdio.c
  src/stdio/stdio2.c
  src/ctype/ctype.c
  src/math/math.c
  src/time/time.c
  src/locale/locale.c
)
target_include_directories(cplus_hardened_libc PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_options(cplus_hardened_libc PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS cplus_hardened_libc ARCHIVE DESTINATION lib)
install(DIRECTORY install-include/ DESTINATION include/cplus-libc)
install(DIRECTORY contracts/ DESTINATION share/cplus-libc/contracts)
install(FILES README.md DESTINATION share/cplus-libc)
