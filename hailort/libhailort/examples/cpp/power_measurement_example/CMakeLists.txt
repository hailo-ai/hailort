cmake_minimum_required(VERSION 3.5.0)

project(hailort-examples-cpp-power-measurement)

find_package(HailoRT 5 REQUIRED)

add_executable(cpp_power_measurement_example power_measurement_example.cpp)
target_link_libraries(cpp_power_measurement_example PRIVATE HailoRT::libhailort)

if(WIN32)
    target_compile_options(cpp_power_measurement_example PRIVATE
        /DWIN32_LEAN_AND_MEAN
        /DNOMINMAX                  # NOMINMAX is required in order to play nice with std::min/std::max (otherwise Windows.h defines it's own)
        /wd4201 /wd4251
    )
endif()

set_target_properties(cpp_power_measurement_example PROPERTIES CXX_STANDARD 17)