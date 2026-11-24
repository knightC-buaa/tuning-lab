cmake_minimum_required(VERSION 3.25)
project(perf_opt_overview LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(Threads REQUIRED)

add_compile_options(-O2 -g -Wall -Wextra)

add_library(perfbench STATIC common/bench.cpp common/cpu.cpp)
target_include_directories(perfbench PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(perfbench PUBLIC Threads::Threads)

enable_testing()
include(cmake/AddExperiment.cmake)
add_subdirectory(exp)
add_subdirectory(tests)
