cmake_minimum_required(VERSION 3.24)
project(martingale VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler check — accept Clang or GCC (both support C++23)
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(WARNING "Only Clang or GCC tested; continuing with ${CMAKE_CXX_COMPILER_ID}")
endif()

set(MARTINGALE_WARN_FLAGS
    -Wall -Wextra -Wpedantic
    -Wunused-parameter -Wunused-variable -Wunused-function
    -Wunused-local-typedef -Wshadow -Wnon-virtual-dtor
    -Wcast-align -Woverloaded-virtual -Wnull-dereference
    -Wdouble-promotion -Wformat=2
)

set(MARTINGALE_STRICT_FLAGS
    -fno-rtti
    -fno-exceptions
)

# Find packages using system installs (no Conan required)
find_package(simdjson REQUIRED)
# Use pkg-config for CURL so we get the shared library + transitive deps
find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
find_package(zstd REQUIRED)
find_package(Curses REQUIRED)

option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
    enable_testing()
    find_package(GTest REQUIRED)
endif()

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
