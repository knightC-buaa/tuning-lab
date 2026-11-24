add_executable(test_bench test_bench.cpp)
target_link_libraries(test_bench PRIVATE perfbench)
add_test(NAME bench_unit COMMAND test_bench)

add_executable(test_pinning test_pinning.cpp)
target_link_libraries(test_pinning PRIVATE perfbench)
add_test(NAME pinning COMMAND test_pinning)

add_test(NAME report_unit
         COMMAND ${Python3_EXECUTABLE} -m unittest discover
                 -s tests -p test_report.py -v)
set_tests_properties(report_unit PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME verify_unit
         COMMAND ${Python3_EXECUTABLE} -m unittest discover
                 -s tests -p test_verify.py -v)
set_tests_properties(verify_unit PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
