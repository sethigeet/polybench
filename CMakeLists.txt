cmake_minimum_required(VERSION 3.27)
project(algobench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

set(PYBIND11_FINDPYTHON NEW)
FetchContent_Declare(
  pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11.git
  GIT_TAG v3.0.1
)

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.12.0
)

FetchContent_MakeAvailable(pybind11 spdlog)

find_package(Python 3.10 COMPONENTS Interpreter Development REQUIRED)

# Core library
add_library(algobench_core 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dual_layer_book.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/exchange.cpp
)

target_include_directories(algobench_core PUBLIC src)
target_link_libraries(algobench_core PUBLIC pybind11::embed spdlog::spdlog)

# Main executable
add_executable(algobench 
    src/main.cpp
    src/bindings.cpp
)
target_link_libraries(algobench PRIVATE algobench_core spdlog::spdlog)
