cmake_minimum_required(VERSION 3.25)
project(perf_opt_overview LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(Threads REQUIRED)

option(PERF_WARNINGS_AS_ERRORS "把编译器告警当作错误（strict 预设使用）" OFF)
option(PERF_SANITIZE "构建 ASan+UBSan 版本（仅用于正确性验证，绝不能用于计时）" OFF)

add_compile_options(
    -O2 -g
    -Wall -Wextra -Wpedantic -Wshadow
    -Wnull-dereference -Wformat=2 -Wimplicit-fallthrough -Wdouble-promotion)

if(PERF_WARNINGS_AS_ERRORS)
  add_compile_options(-Werror)
endif()

if(PERF_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all
                      -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(perfbench STATIC common/bench.cpp common/cpu.cpp)
target_include_directories(perfbench PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(perfbench PUBLIC Threads::Threads)

enable_testing()
include(cmake/AddExperiment.cmake)
add_subdirectory(exp)
add_subdirectory(tests)
