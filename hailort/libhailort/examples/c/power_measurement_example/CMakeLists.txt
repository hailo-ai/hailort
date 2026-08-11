cmake_minimum_required(VERSION 3.5.0)

project(hailort-examples-c-power-measurement)

find_package(HailoRT 5 REQUIRED)

SET_SOURCE_FILES_PROPERTIES(power_measurement_example.c PROPERTIES LANGUAGE C)

add_executable(c_power_measurement_example power_measurement_example.c)
target_link_libraries(c_power_measurement_example PRIVATE HailoRT::libhailort)
target_include_directories(c_power_measurement_example PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../common")

if(WIN32)
    target_compile_options(c_power_measurement_example PRIVATE
        /DWIN32_LEAN_AND_MEAN
        /wd4201 /wd4251
    )
endif()