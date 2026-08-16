cmake_minimum_required(VERSION 3.5.0)

project(hailort-examples-c-raw-async-streams-single-thread)

find_package(HailoRT 5 REQUIRED)

SET_SOURCE_FILES_PROPERTIES(raw_async_streams_single_thread_example.c PROPERTIES LANGUAGE C)

add_executable(c_raw_async_streams_single_thread_example raw_async_streams_single_thread_example.c)
target_link_libraries(c_raw_async_streams_single_thread_example PRIVATE HailoRT::libhailort)
target_include_directories(c_raw_async_streams_single_thread_example PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../common")

if (WIN32)
    target_compile_options(c_raw_async_streams_single_thread_example PRIVATE
        /wd4201 /wd4251
    )
endif()