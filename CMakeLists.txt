cmake_minimum_required(VERSION 3.20)
project(cplus
  VERSION 0.1.0
  DESCRIPTION "C+ : C with modern memory safety, deterministic RAII, and lightweight classes"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Locate LLVM + Clang development libraries.
#
# C+ is a thin feature layer over an existing open-source C toolchain.  It
# lowers the small set of C+ additions to plain C and hands that C to the
# host Clang driver, which does parsing, type checking, optimisation, code
# generation and linking.  C itself is never re-implemented.
#
# LLVM/Clang development libraries are linked for the driver's own support
# code (file/path/process helpers).  A *static* LLVM/Clang build is used
# because Homebrew's `libLLVM.dylib` may be a stub and macOS SDK versions do
# not always match the toolchain's min-version.
# ---------------------------------------------------------------------------
# Force static LLVM linkage.  Homebrew's shared libLLVM.dylib may be a stub,
# and the static archives are always complete.
set(LLVM_LINK_LLVM_DYLIB OFF CACHE BOOL "" FORCE)
set(LLVM_BUILD_LLVM_DYLIB OFF CACHE BOOL "" FORCE)
find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

# The imported target `LLVM::LLVM` is the shared libLLVM dylib, which on this
# machine is a 0-byte stub and would abort the link.  Every LLVM component
# target transitively depends on it, so we replace it with the static
# component archives.  See docs/ARCHITECTURE.md for the full rationale.
if(TARGET LLVM)
  get_target_property(_llvm_dylib LLVM IMPORTED_LOCATION_RELEASE)
  set(_llvm_usable FALSE)
  if(_llvm_dylib AND EXISTS "${_llvm_dylib}")
    file(SIZE "${_llvm_dylib}" _llvm_size)
    if(_llvm_size GREATER 0)
      set(_llvm_usable TRUE)
    endif()
  endif()
  if(_llvm_usable)
    message(STATUS "libLLVM.dylib is usable: ${_llvm_dylib}")
  else()
    message(STATUS "libLLVM.dylib is missing or empty; using static archives")
    # Build the static component list up-front and re-point LLVM::LLVM.
    execute_process(
      COMMAND ${LLVM_TOOLS_BINARY_DIR}/llvm-config --link-static --libs all
      OUTPUT_VARIABLE LLVM_STATIC_LIBS_RAW
      RESULT_VARIABLE LLVM_CONFIG_RC
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT LLVM_CONFIG_RC EQUAL 0)
      message(FATAL_ERROR "llvm-config --link-static --libs all failed")
    endif()
    separate_arguments(LLVM_STATIC_LIBS_LIST NATIVE_COMMAND "${LLVM_STATIC_LIBS_RAW}")
    set(LLVM_STATIC_ARCHIVES "")
    foreach(item ${LLVM_STATIC_LIBS_LIST})
      if(item MATCHES "^-l(.+)$")
        set(comp "${CMAKE_MATCH_1}")
        if(EXISTS "${LLVM_LIBRARY_DIR}/lib${comp}.a")
          list(APPEND LLVM_STATIC_ARCHIVES "${LLVM_LIBRARY_DIR}/lib${comp}.a")
        endif()
      endif()
    endforeach()
    set_target_properties(LLVM PROPERTIES
      IMPORTED_LOCATION "${LLVM_LIBRARY_DIR}/libLLVMCore.a"
      IMPORTED_LOCATION_RELEASE "${LLVM_LIBRARY_DIR}/libLLVMCore.a"
      INTERFACE_LINK_LIBRARIES "")
    target_link_libraries(LLVM INTERFACE ${LLVM_STATIC_ARCHIVES})
  endif()
endif()

message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")
message(STATUS "Using Clang from ${Clang_DIR}")

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")

add_library(cplus_core STATIC
  compiler/Diagnostics.cpp
  compiler/SourceManager.cpp
  compiler/Sema/SemaCPlus.cpp
  compiler/Sema/CPlusLexer.cpp
  compiler/Sema/CPlusRewriter.cpp
  compiler/Sema/RaiiLowering.cpp
  compiler/Sema/StaticBoundsCheck.cpp
  compiler/Sema/AstSafetyCheck.cpp
  compiler/Sema/TypeChecker.cpp
  runtime/CPlusRuntime.cpp
  compiler/Driver/Driver.cpp)

target_include_directories(cplus_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/compiler
  ${LLVM_INCLUDE_DIRS}
  ${CLANG_INCLUDE_DIRS})

target_compile_definitions(cplus_core PUBLIC
  ${LLVM_DEFINITIONS_LIST}
  CLANG_CPLUS_LANG_STRING="C+"
  CPLUS_VERSION_STRING="${PROJECT_VERSION}")

target_compile_options(cplus_core PRIVATE -fno-rtti)

# LLVM is built without RTTI; matching that avoids ODR/UB issues.
if(LLVM_ENABLE_RTTI)
  target_compile_options(cplus_core PRIVATE -frtti)
endif()

# Link every static Clang archive we need.  We link the whole set of Clang
# component libraries because the dependency graph between Clang's static
# archives is not expressible in a single CMake target in a portable way.
# Only static archives: the shared libclang-cpp.dylib would drag in the
# (possibly stub) shared libLLVM.dylib.
file(GLOB CLANG_STATIC_LIBS "${CLANG_INSTALL_PREFIX}/lib/libclang*.a")
list(SORT CLANG_STATIC_LIBS)
set(CLANG_LINK_LIBS "")
foreach(lib ${CLANG_STATIC_LIBS})
  get_filename_component(base "${lib}" NAME_WE)
  string(REGEX REPLACE "^lib" "" base "${base}")
  list(APPEND CLANG_LINK_LIBS "${base}")
endforeach()
# The `clang` and `clang-cpp` CMake targets are shared libraries that pull in
# libLLVM.dylib; drop them so we link purely against the static archives.
list(REMOVE_ITEM CLANG_LINK_LIBS "clang")
list(REMOVE_ITEM CLANG_LINK_LIBS "clang-cpp")

# Every Clang component target lists `LLVM` and/or `$<LINK_ONLY:clang-cpp>` in
# its INTERFACE_LINK_LIBRARIES, which drags in the shared libLLVM.dylib.
# Strip them from the imported targets in the Clang package.
# The Clang package exports the full list of its imported targets.
set(_clang_pkg_targets ${CLANG_EXPORTED_TARGETS})
set(_clang_pkg_targets ${_clang_pkg_targets} clangAPINotes clangSupport
    clangUnifiedSymbolResolution clangCodeGen clangDynamicASTMatchers
    clangAnalysisFlowSensitive clangAnalysisFlowSensitiveModels
    clangAnalysisLifetimeSafety clangStaticAnalyzerCore
    clangStaticAnalyzerCheckers clangStaticAnalyzerFrontend
    clangScalableStaticAnalysisCore clangScalableStaticAnalysisAnalyses
    clangScalableStaticAnalysisFrontend
    clangScalableStaticAnalysisSourceTransformation
    clangScalableStaticAnalysisTool)

foreach(_t ${_clang_pkg_targets})
  if(TARGET ${_t})
    get_target_property(_il ${_t} INTERFACE_LINK_LIBRARIES)
    if(_il)
      set(_cleaned "")
      foreach(_dep ${_il})
        string(REPLACE "$<LINK_ONLY:" "" _d "${_dep}")
        string(REPLACE ">" "" _d "${_d}")
        # "clang" and "clang-cpp" are the shared Clang libraries; they would
        # pull in libLLVM.dylib.  "LLVM" is kept: we have repointed that
        # imported target at the static component archives.
        if(_d STREQUAL "clang" OR _d STREQUAL "clang-cpp")
          continue()
        endif()
        list(APPEND _cleaned "${_dep}")
      endforeach()
      set_target_properties(${_t} PROPERTIES
        INTERFACE_LINK_LIBRARIES "${_cleaned}")
    endif()
  endif()
endforeach()

target_link_libraries(cplus_core PUBLIC
  ${CLANG_LINK_LIBS})

# LLVM's own system dependencies (zlib, zstd, curl, ...) on this platform.
execute_process(
  COMMAND ${LLVM_TOOLS_BINARY_DIR}/llvm-config --link-static --system-libs
  OUTPUT_VARIABLE LLVM_SYSTEM_LIBS_RAW
  RESULT_VARIABLE LLVM_SYS_RC
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT LLVM_SYS_RC EQUAL 0)
  message(FATAL_ERROR "llvm-config --link-static --system-libs failed")
endif()
separate_arguments(LLVM_SYSTEM_LIBS_RAW_LIST NATIVE_COMMAND
  "${LLVM_SYSTEM_LIBS_RAW}")
# Resolve bare -l names to real library paths; some (zstd) live outside the
# default linker search path.
set(LLVM_SYSTEM_LIBS_LIST "")
foreach(_sl ${LLVM_SYSTEM_LIBS_RAW_LIST})
  if(_sl MATCHES "^-l(.+)$")
    set(_name "${CMAKE_MATCH_1}")
    find_library(_found_${_name} NAMES ${_name}
      PATHS /opt/homebrew/lib /usr/local/lib
      NO_DEFAULT_PATH)
    if(_found_${_name})
      list(APPEND LLVM_SYSTEM_LIBS_LIST "${_found_${_name}}")
    else()
      list(APPEND LLVM_SYSTEM_LIBS_LIST "${_sl}")
    endif()
  else()
    list(APPEND LLVM_SYSTEM_LIBS_LIST "${_sl}")
  endif()
endforeach()
# Homebrew installs zstd/zlib outside the default search path.
find_library(CPLUS_ZSTD_LIB zstd PATHS /opt/homebrew/lib /usr/local/lib)
find_library(CPLUS_Z_LIB z PATHS /opt/homebrew/lib /usr/local/lib)
if(CPLUS_ZSTD_LIB)
  list(APPEND LLVM_SYSTEM_LIBS_LIST "${CPLUS_ZSTD_LIB}")
else()
  list(APPEND LLVM_SYSTEM_LIBS_LIST zstd)
endif()
if(CPLUS_Z_LIB)
  list(APPEND LLVM_SYSTEM_LIBS_LIST "${CPLUS_Z_LIB}")
else()
  list(APPEND LLVM_SYSTEM_LIBS_LIST z)
endif()
target_link_libraries(cplus_core PUBLIC ${LLVM_SYSTEM_LIBS_LIST})
if(APPLE)
  target_link_libraries(cplus_core PUBLIC xcselect)
endif()

# ---------------------------------------------------------------------------
# The `c+` driver executable
# ---------------------------------------------------------------------------
add_executable(c+ compiler/Main.cpp)
target_link_libraries(c+ PRIVATE cplus_core)
set_target_properties(c+ PROPERTIES OUTPUT_NAME "c+")

# ---------------------------------------------------------------------------
# The C+ runtime (linked into produced programs)
# ---------------------------------------------------------------------------
add_library(cplus_runtime STATIC runtime/cplus_runtime.c)
target_include_directories(cplus_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/runtime/include)
target_link_libraries(cplus_runtime PUBLIC m)
if(APPLE)
  target_link_libraries(cplus_runtime PUBLIC pthread)
endif()

# The driver needs to locate the host C compiler, the C+ runtime archive and
# the C+ headers at run time.  The compile-time values are defaults; all of
# them can be overridden through environment variables (CPLUS_CLANG,
# CPLUS_INCLUDE, CPLUS_RUNTIME_LIB).
find_program(CPLUS_CLANG_BIN NAMES clang
  PATHS /usr/bin /usr/local/bin
  NO_DEFAULT_PATH)
if(NOT CPLUS_CLANG_BIN)
  set(CPLUS_CLANG_BIN "clang")
endif()
message(STATUS "C+ backend C compiler: ${CPLUS_CLANG_BIN}")

target_compile_definitions(cplus_core PRIVATE
  CPLUS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  CPLUS_INCLUDE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/runtime/include"
  CPLUS_CLANG_BIN="${CPLUS_CLANG_BIN}"
  CPLUS_RUNTIME_LIB="$<TARGET_FILE:cplus_runtime>")
add_dependencies(c+ cplus_runtime)

install(TARGETS c+ cplus_runtime
  RUNTIME DESTINATION bin
  ARCHIVE DESTINATION lib)
install(FILES runtime/include/cplus/cplus.h DESTINATION include/cplus)

# ---------------------------------------------------------------------------
# CTest wrappers around the executable test and demo suites
# ---------------------------------------------------------------------------
enable_testing()
add_test(NAME cplus_tests
  COMMAND ${CMAKE_COMMAND} -E env CPLUS_BIN=$<TARGET_FILE:c+>
          /bin/sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.sh)
add_test(NAME cplus_demos
  COMMAND ${CMAKE_COMMAND} -E env CPLUS_BIN=$<TARGET_FILE:c+>
          /bin/sh ${CMAKE_CURRENT_SOURCE_DIR}/demos/run_demos.sh)
