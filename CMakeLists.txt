cmake_minimum_required(VERSION 3.25) # FetchContent SYSTEM: our -Werror must not apply to dependency headers
project(lock_free_ring_buffer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LFRB_BUILD_TESTS "Build GoogleTest unit/stress tests" ON)
option(LFRB_BUILD_BENCHMARKS "Build throughput and latency benchmarks" ON)
set(LFRB_SANITIZER "" CACHE STRING "Sanitizer build: '' | thread | address (address also enables UBSan)")

if(LFRB_SANITIZER STREQUAL "thread")
    add_compile_options(-fsanitize=thread -g -O1)
    add_link_options(-fsanitize=thread)
elseif(LFRB_SANITIZER STREQUAL "address")
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g -O1)
    add_link_options(-fsanitize=address,undefined)
elseif(NOT LFRB_SANITIZER STREQUAL "")
    message(FATAL_ERROR "LFRB_SANITIZER must be empty, 'thread' or 'address'")
endif()

find_package(Threads REQUIRED)

add_library(lock_free_ring_buffer INTERFACE)
target_include_directories(lock_free_ring_buffer INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_options(lock_free_ring_buffer INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Werror>
)
target_link_libraries(lock_free_ring_buffer INTERFACE Threads::Threads)

include(FetchContent)

if(LFRB_BUILD_TESTS)
    enable_testing()
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        FetchContent_Declare(googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG        v1.15.2
            GIT_SHALLOW    TRUE
            SYSTEM)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    add_executable(ring_buffer_tests tests/ring_buffer_tests.cpp)
    target_link_libraries(ring_buffer_tests PRIVATE lock_free_ring_buffer GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(ring_buffer_tests DISCOVERY_TIMEOUT 60)
endif()

if(LFRB_BUILD_BENCHMARKS)
    find_package(benchmark QUIET)
    if(NOT benchmark_FOUND)
        FetchContent_Declare(googlebenchmark
            GIT_REPOSITORY https://github.com/google/benchmark.git
            GIT_TAG        v1.9.5
            GIT_SHALLOW    TRUE
            SYSTEM)
        set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
        set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE) # its own -Werror trips on newer compilers
        set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googlebenchmark)
    endif()

    add_executable(ring_buffer_bench benchmark/benchmark.cpp)
    target_link_libraries(ring_buffer_bench PRIVATE lock_free_ring_buffer benchmark::benchmark)

    add_executable(ring_buffer_latency benchmark/latency.cpp)
    target_link_libraries(ring_buffer_latency PRIVATE lock_free_ring_buffer)

    foreach(t ring_buffer_bench ring_buffer_latency)
        target_compile_options(${t} PRIVATE -O3 -march=native)
    endforeach()
endif()
