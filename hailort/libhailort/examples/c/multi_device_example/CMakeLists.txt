cmake_minimum_required(VERSION 3.5.0)

project(hailort-examples-c-multi-device)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(HailoRT 5 REQUIRED)

SET_SOURCE_FILES_PROPERTIES(multi_device_example.c PROPERTIES LANGUAGE C)

add_executable(c_multi_device_example multi_device_example.c)
target_link_libraries(c_multi_device_example PRIVATE HailoRT::libhailort Threads::Threads)
target_include_directories(c_multi_device_example PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../common")

if(WIN32)
    target_compile_options(c_multi_device_example PRIVATE
        /DWIN32_LEAN_AND_MEAN
        /DNOMINMAX                  # NOMINMAX is required in order to play nice with std::min/std::max (otherwise Windows.h defines it's own)
        /wd4201 /wd4251
    )
endif()