# The Performance and Health Stats Query Example is only available on UNIX systems
if(UNIX)
cmake_minimum_required(VERSION 3.5.0)

find_package(HailoRT 5.0.0 EXACT REQUIRED)

add_executable(cpp_query_performance_and_health_stats_example query_performance_and_health_stats_example.cpp)
target_link_libraries(cpp_query_performance_and_health_stats_example PRIVATE HailoRT::libhailort)


set_target_properties(cpp_query_performance_and_health_stats_example PROPERTIES CXX_STANDARD 14)

endif()

