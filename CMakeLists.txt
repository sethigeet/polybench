cmake_minimum_required(VERSION 3.27)
project(polybench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
