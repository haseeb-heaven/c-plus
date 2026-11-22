cmake_minimum_required(VERSION 3.16)
project(cplus_hardened_libc C)
set(CMAKE_C_STANDARD 99)
find_package(Threads REQUIRED)

# Optional AddressSanitizer build. OFF by default so ordinary release builds
# never need ASan headers or runtime. The allocators delegate every logical
# allocation to a distinct system malloc block, so ASan's own interceptors
# supply redzones/quarantine; no manual memory poisoning is used.
option(CPLUS_LIBC_ENABLE_ASAN "Build the C+ libc with AddressSanitizer instrumentation" OFF)

set(LIBC_SOURCES
  log/log.c
  src/errno/errno.c
  src/stdlib/alloc.c
  src/stdlib/stdlib_extra.c
  src/stdlib/stdlib_extra2.c
  src/stdlib/stdlib_extra3.c
  src/stdlib/stdlib_extra4.c
  src/stdlib/stdlib_extra5.c
  src/stdlib/stdlib_extra6.c
  src/stdlib/assert_extra.c
  src/stdlib/musl_stdlib_extra.c
  src/string/memory.c
  src/string/bounded.c
  src/string/string.c
  src/string/extra.c
  src/string/extra2.c
  src/string/extra3.c
  src/string/string_extra4.c
  src/string/string_extra5.c
  src/string/string_extra6.c
  src/string/strtok.c
  src/string/musl_extra.c
  src/string/strndup.c
  src/string/strerror.c
  src/stdio/stdio.c
  src/stdio/stdio2.c
  src/stdio/stdio3.c
  src/ctype/ctype.c
  src/wchar/wchar.c
  src/wctype/wctype.c
  src/ctype/ctype2.c
  src/ctype/ctype3.c
  src/math/math.c
  src/math/math2.c
  src/math/math3.c
  src/math/math4.c
  src/time/time.c
  src/time/time_extra.c
  src/locale/locale.c
  src/signal/signal.c
  src/signal/signal2.c
  src/fenv/fenv.c
  src/complex/complex.c
  src/thread/thread.c
  src/fenv/fenv_extra.c
  src/time/time_ascii.c
  src/wchar/wchar_numeric.c
  src/wchar/wchar_multibyte.c
  src/wchar/wchar_format.c
  src/wctype/wctype_extra.c
  src/complex/complex_extra.c
  src/complex/complex_inverse.c
  src/math/math_gaps.c
  src/math/math_float.c
  src/math/math_ldouble.c
  src/stdlib/stdlib_c17.c
  src/stdlib/inttypes_wide.c
)

add_library(cplus_hardened_libc STATIC ${LIBC_SOURCES})
set(LIBC_INSTALL_TARGETS cplus_hardened_libc)
# A sanitizer-instrumented dylib cannot resolve the ASan runtime on macOS;
# the ASan configuration therefore produces the static library only.
if(NOT CPLUS_LIBC_ENABLE_ASAN)
  add_library(cplus_hardened_libc_shared SHARED ${LIBC_SOURCES})
  set_target_properties(cplus_hardened_libc_shared PROPERTIES OUTPUT_NAME cplus_hardened_libc)
  list(APPEND LIBC_INSTALL_TARGETS cplus_hardened_libc_shared)
endif()

foreach(tgt IN ITEMS ${LIBC_INSTALL_TARGETS})
  target_include_directories(${tgt}
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/install-include>
      $<INSTALL_INTERFACE:include/cplus-libc>
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic)
  target_link_libraries(${tgt} PRIVATE m Threads::Threads)
  if(CPLUS_LIBC_ENABLE_ASAN)
    target_compile_options(${tgt} PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(${tgt} PRIVATE INTERFACE -fsanitize=address)
  endif()
endforeach()

install(TARGETS ${LIBC_INSTALL_TARGETS}
  EXPORT cplus_hardened_libcTargets
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
install(DIRECTORY install-include/ DESTINATION include/cplus-libc)
install(DIRECTORY contracts/ DESTINATION share/cplus-libc/contracts)
install(FILES README.md DESTINATION share/cplus-libc)
install(EXPORT cplus_hardened_libcTargets
  NAMESPACE cplus::
  DESTINATION lib/cmake/cplus_hardened_libc
)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cplus_hardened_libcConfig.cmake"
  "include(\"\${CMAKE_CURRENT_LIST_DIR}/cplus_hardened_libcTargets.cmake\")\n"
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/cplus_hardened_libcConfig.cmake"
  DESTINATION lib/cmake/cplus_hardened_libc
)
