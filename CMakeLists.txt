cmake_minimum_required(VERSION 3.16)

project(parallel_numerics_bench
        VERSION 1.0.0
        DESCRIPTION "Parallel Numerics Bench: reproducible serial/parallel accuracy and scaling"
        LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (single-config generators)" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

# Header-only core: thread pool, counter RNG, workloads, timing.
add_library(pnb_headers INTERFACE)
target_include_directories(pnb_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(pnb_headers INTERFACE cxx_std_17)
target_link_libraries(pnb_headers INTERFACE Threads::Threads)
if(MSVC)
  target_compile_options(pnb_headers INTERFACE /W4)
else()
  target_compile_options(pnb_headers INTERFACE -Wall -Wextra -Wpedantic)
endif()

# Measurement driver (writes the CSVs under docs/results by default).
add_executable(pnb_bench src/bench_main.cpp)
target_link_libraries(pnb_bench PRIVATE pnb_headers)
target_compile_definitions(pnb_bench PRIVATE PNB_BUILD_TYPE="${CMAKE_BUILD_TYPE}")

include(CTest)
if(BUILD_TESTING)
  set(PNB_TESTS
      test_headers
      test_pool
      test_counter_rng
      test_partition
      test_equivalence_mc
      test_equivalence_matmul
      test_determinism
      test_validation
      test_timing
  )
  foreach(pnb_test IN LISTS PNB_TESTS)
    add_executable(${pnb_test} tests/${pnb_test}.cpp)
    target_link_libraries(${pnb_test} PRIVATE pnb_headers)
    add_test(NAME ${pnb_test} COMMAND ${pnb_test})
    set_tests_properties(${pnb_test} PROPERTIES TIMEOUT 300)
  endforeach()
endif()
