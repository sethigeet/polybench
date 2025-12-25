cmake_minimum_required(VERSION 3.15)
project(algobench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11.git
  GIT_TAG v2.11.1
)
FetchContent_MakeAvailable(pybind11)

find_package(Python 3.10 COMPONENTS Interpreter Development REQUIRED)

# Core library
add_library(algobench_core 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mock_exchange.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/order_book.cpp
)

target_include_directories(algobench_core PUBLIC src)
target_link_libraries(algobench_core PUBLIC pybind11::embed)

# Main executable
add_executable(algobench 
    src/main.cpp
    src/bindings.cpp
)
target_link_libraries(algobench PRIVATE algobench_core)
