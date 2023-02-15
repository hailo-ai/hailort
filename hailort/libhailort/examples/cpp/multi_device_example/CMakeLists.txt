cmake_minimum_required(VERSION 3.0.0)

find_package(Threads REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(HailoRT 4.12.1 EXACT REQUIRED)

add_executable(cpp_multi_device_example multi_device_example.cpp)
target_link_libraries(cpp_multi_device_example PRIVATE HailoRT::libhailort Threads::Threads)

if(WIN32)
    target_compile_options(cpp_multi_device_example PRIVATE
        /DWIN32_LEAN_AND_MEAN
        /DNOMINMAX                  # NOMINMAX is required in order to play nice with std::min/std::max (otherwise Windows.h defines it's own)
        /wd4201 /wd4251
    )
endif()

set_target_properties(cpp_multi_device_example PROPERTIES CXX_STANDARD 14)
