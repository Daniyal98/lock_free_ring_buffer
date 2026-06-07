cmake_minimum_required(VERSION 3.20)
project(lock_free_ring_buffer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(spsc_ring_buffer INTERFACE)
target_include_directories(spsc_ring_buffer INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_options(spsc_ring_buffer INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
)

option(LFRB_BUILD_TESTS "Build tests" ON)

if(LFRB_BUILD_TESTS)
    enable_testing()
    find_package(GTest QUIET)

    if(GTest_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/spsc_tests.cpp")
        add_executable(spsc_tests tests/spsc_tests.cpp)
        target_link_libraries(spsc_tests PRIVATE spsc_ring_buffer GTest::gtest_main)
        include(GoogleTest)
        gtest_discover_tests(spsc_tests)
    elseif(NOT GTest_FOUND)
        message(STATUS "GTest not found — skipping tests. Install it or set LFRB_BUILD_TESTS=OFF.")
    endif()
endif()
