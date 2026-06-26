cmake_minimum_required(VERSION 3.24)
project(martingale VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Only Clang++ is supported")
endif()

set(MARTINGALE_WARN_FLAGS
    -Wall -Wextra -Wpedantic -Werror
    -Wunused-parameter -Wunused-variable -Wunused-function
    -Wunused-local-typedef -Wconversion -Wsign-conversion
    -Wfloat-equal -Wshadow -Wnon-virtual-dtor -Wold-style-cast
    -Wcast-align -Woverloaded-virtual -Wnull-dereference
    -Wdouble-promotion -Wformat=2
)

set(MARTINGALE_STRICT_FLAGS
    -fno-rtti
    -fno-exceptions
)

include(${CMAKE_BINARY_DIR}/conan_toolchain.cmake OPTIONAL)

find_package(simdjson REQUIRED)
find_package(CURL REQUIRED)
find_package(zstd REQUIRED)

add_subdirectory(src)

option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
    enable_testing()
    find_package(GTest REQUIRED)
    find_package(benchmark REQUIRED)
    add_subdirectory(tests)
endif()
