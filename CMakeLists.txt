cmake_minimum_required(VERSION 3.27)
project(polybench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable compile commands for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_definitions(-DSOME_DEFINITION)

# Profile-Guided Optimization (PGO) support
option(PGO_GENERATE "Build with PGO instrumentation (run this build to generate profile data)" OFF)
option(PGO_USE "Build using PGO profile data (run after PGO_GENERATE build has collected data)" OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  if(PGO_GENERATE)
    message(STATUS "PGO: Generating profile instrumentation")
    add_compile_options(-fprofile-generate=${CMAKE_BINARY_DIR}/pgo)
    add_link_options(-fprofile-generate=${CMAKE_BINARY_DIR}/pgo)
  elseif(PGO_USE)
    message(STATUS "PGO: Using profile data from ${CMAKE_BINARY_DIR}/pgo")
    add_compile_options(-fprofile-use=${CMAKE_BINARY_DIR}/pgo -fprofile-correction)
    add_link_options(-fprofile-use=${CMAKE_BINARY_DIR}/pgo)
  endif()
endif()

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

# io_uring support (Linux only, requires liburing-dev)
set(HAS_IO_URING OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(URING IMPORTED_TARGET liburing>=2.4)
    if(URING_FOUND)
      set(HAS_IO_URING ON)
      message(STATUS "liburing ${URING_VERSION} found — io_uring transport enabled")
    else()
      message(STATUS "liburing not found — io_uring transport disabled (install liburing-dev)")
    endif()
  endif()
endif()

find_package(OpenSSL REQUIRED)

# Core library
add_library(polybench_core
    # Engine
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine.cpp
    # Trading
    ${CMAKE_CURRENT_SOURCE_DIR}/src/trading/exchange.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/trading/market_book.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/trading/portfolio_tracker.cpp
    # Transport
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport/ingest_pipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport/json_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport/polymarket_ws.cpp
    # Utils
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils/logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils/perf_stats.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils/thread.cpp
)

if(HAS_IO_URING)
  target_sources(polybench_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport/io_uring_ws.cpp
  )
  target_link_libraries(polybench_core PUBLIC PkgConfig::URING OpenSSL::SSL OpenSSL::Crypto)
  target_compile_definitions(polybench_core PUBLIC HAS_IO_URING=1)
endif()

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
        tests/test_market_data_transport.cpp
        tests/test_portfolio_tracker.cpp
        tests/test_json_parser.cpp
        tests/test_ws_frame_parser.cpp
        tests/test_io_uring_ws.cpp
    )
    target_link_libraries(polybench_tests PRIVATE polybench_core GTest::gtest_main)
    target_include_directories(polybench_tests PRIVATE src)

    if(HAS_IO_URING)
      target_link_libraries(polybench_tests PRIVATE PkgConfig::URING OpenSSL::SSL OpenSSL::Crypto)
    endif()

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
        benchmarks/bench_ingest_pipeline.cpp
        benchmarks/bench_json_parser.cpp
        benchmarks/bench_logger.cpp
        benchmarks/bench_market_book.cpp
        benchmarks/bench_python_callback.cpp
        benchmarks/bench_ring_buffer.cpp
        benchmarks/bench_small_vector.cpp
    )
    target_link_libraries(polybench_benchmarks PRIVATE polybench_core benchmark::benchmark)
    target_include_directories(polybench_benchmarks PRIVATE src)
endif()
