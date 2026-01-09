cmake_minimum_required(VERSION 3.27)
project(polybench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable compile commands for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_definitions(-DSOME_DEFINITION)

include(FetchContent)

set(PYBIND11_FINDPYTHON NEW)
FetchContent_Declare(
  pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11.git
  GIT_TAG v3.0.1
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.12.0
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  simdjson
  GIT_REPOSITORY https://github.com/simdjson/simdjson.git
  GIT_TAG v4.2.4
  GIT_SHALLOW TRUE
)

set(USE_TLS ON CACHE BOOL "Enable TLS support")
FetchContent_Declare(
  ixwebsocket
  GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
  GIT_TAG v11.4.5
  GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(pybind11 spdlog simdjson ixwebsocket)

# pybind11 automatically finds the Python interpreter and development files
# find_package(Python 3.10 COMPONENTS Interpreter Development REQUIRED)

# Core library
add_library(polybench_core 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/exchange.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/json_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/market_book.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/polymarket_ws.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/portfolio_tracker.cpp
)

target_include_directories(polybench_core PUBLIC src)
target_link_libraries(polybench_core PUBLIC 
    pybind11::embed 
    spdlog::spdlog 
    simdjson::simdjson
    ixwebsocket::ixwebsocket
)

# Main executable
add_executable(polybench 
    src/main.cpp
    src/bindings.cpp
)
target_link_libraries(polybench PRIVATE polybench_core spdlog::spdlog)

# Testing (disabled by default for faster builds)
option(BUILD_TESTS "Build the test suite" OFF)

if(BUILD_TESTS)
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.15.2
      GIT_SHALLOW TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      # Adds -fprofile-arcs -ftest-coverage and links with --coverage
      add_compile_options(--coverage -O0 -g)
      add_link_options(--coverage)
    endif()

    add_executable(polybench_tests
        tests/test_fixed_string.cpp
        tests/test_small_vector.cpp
        tests/test_ring_buffer.cpp
        tests/test_market_book.cpp
        tests/test_exchange.cpp
        tests/test_portfolio_tracker.cpp
        tests/test_json_parser.cpp
    )
    target_link_libraries(polybench_tests PRIVATE polybench_core GTest::gtest_main)
    target_include_directories(polybench_tests PRIVATE src)

    include(GoogleTest)
    gtest_discover_tests(polybench_tests)
endif()

# Benchmarks (disabled by default)
option(BUILD_BENCHMARKS "Build the benchmark suite" OFF)

if(BUILD_BENCHMARKS)
    FetchContent_Declare(
      benchmark
      GIT_REPOSITORY https://github.com/google/benchmark.git
      GIT_TAG v1.9.1
      GIT_SHALLOW TRUE
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)

    add_executable(polybench_benchmarks
        benchmarks/bench_main.cpp
        benchmarks/bench_exchange.cpp
        benchmarks/bench_fixed_string.cpp
        benchmarks/bench_json_parser.cpp
        benchmarks/bench_market_book.cpp
        benchmarks/bench_ring_buffer.cpp
        benchmarks/bench_small_vector.cpp
    )
    target_link_libraries(polybench_benchmarks PRIVATE polybench_core benchmark::benchmark)
    target_include_directories(polybench_benchmarks PRIVATE src)
endif()
