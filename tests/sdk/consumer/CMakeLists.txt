cmake_minimum_required(VERSION 3.16)
project(cplus_libc_sdk_consumer C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# External-consumer simulation: there are NO source-tree paths here. The
# package is located purely through CMAKE_PREFIX_PATH pointing at a clean
# install prefix produced by the runner script.
find_package(cplus_hardened_libc REQUIRED CONFIG)

# cplus::cplus_hardened_libc is the exported STATIC archive. Its exported
# LINK_ONLY interface references Threads::Threads, so a consumer must resolve
# that imported target before linking (m is a plain system library name).
find_package(Threads REQUIRED)

add_executable(sdk_consumer main.c)
target_link_libraries(sdk_consumer PRIVATE cplus::cplus_hardened_libc)
target_compile_options(sdk_consumer PRIVATE -Wall -Wextra -Wpedantic)
