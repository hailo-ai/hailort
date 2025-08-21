cmake_minimum_required(VERSION 3.5.0)

project(hailort-examples-cpp-query-performance-and-health-stats)

if (NOT UNIX)
    message(FATAL_ERROR "The Performance and Health Stats Query Example is only available on UNIX systems")
endif()

find_package(HailoRT 5 REQUIRED)


add_executable(cpp_query_performance_and_health_stats_example query_performance_and_health_stats_example.cpp)
target_link_libraries(cpp_query_performance_and_health_stats_example PRIVATE HailoRT::libhailort)


set_target_properties(cpp_query_performance_and_health_stats_example PROPERTIES CXX_STANDARD 17)
