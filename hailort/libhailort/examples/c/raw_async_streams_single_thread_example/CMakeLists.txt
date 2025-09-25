cmake_minimum_required(VERSION 3.5.0)

find_package(HailoRT 4.23.0 EXACT REQUIRED)

SET_SOURCE_FILES_PROPERTIES(raw_async_streams_single_thread_example.c PROPERTIES LANGUAGE C)

add_executable(c_raw_async_streams_single_thread_example raw_async_streams_single_thread_example.c)
target_link_libraries(c_raw_async_streams_single_thread_example PRIVATE HailoRT::libhailort)
target_include_directories(c_raw_async_streams_single_thread_example PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../common")

if(WIN32)
    target_compile_options(c_raw_async_streams_single_thread_example PRIVATE
        /DWIN32_LEAN_AND_MEAN
        /DNOMINMAX                  # NOMINMAX is required in order to play nice with std::min/std::max (otherwise Windows.h defines it's own)
        /wd4201 /wd4251
    )
endif()