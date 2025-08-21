cmake_minimum_required(VERSION 3.5.0)

project(hailort-examples-c-raw-streams)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(HailoRT 5 REQUIRED)

SET_SOURCE_FILES_PROPERTIES(raw_streams_example.c PROPERTIES LANGUAGE C)

add_executable(c_raw_streams_example raw_streams_example.c)
target_link_libraries(c_raw_streams_example PRIVATE HailoRT::libhailort Threads::Threads)
target_include_directories(c_raw_streams_example PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../common")

if(WIN32)
    target_compile_options(c_raw_streams_example PRIVATE
        /DWIN32_LEAN_AND_MEAN
        /DNOMINMAX                  # NOMINMAX is required in order to play nice with std::min/std::max (otherwise Windows.h defines it's own)
        /wd4201 /wd4251
    )
endif()