add_executable(test_bench test_bench.cpp)
target_link_libraries(test_bench PRIVATE perfbench)
add_test(NAME bench_unit COMMAND test_bench)

add_executable(test_pinning test_pinning.cpp)
target_link_libraries(test_pinning PRIVATE perfbench)
add_test(NAME pinning COMMAND test_pinning)
