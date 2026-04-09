cmake_minimum_required(VERSION 3.5.0)

project(hailort-examples-c-multi-network-vstream)

find_package(Threads REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(HailoRT 5 REQUIRED)

SET_SOURCE_FILES_PROPERTIES(multi_network_vstream_example.c PROPERTIES LANGUAGE C)

add_executable(c_multi_network_vstream_example multi_network_vstream_example.c)
target_link_libraries(c_multi_network_vstream_example PRIVATE HailoRT::libhailort Threads::Threads)
target_include_directories(c_multi_network_vstream_example PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../common")

if (WIN32)
    target_compile_options(c_multi_network_vstream_example PRIVATE
        /wd4201 /wd4251
    )
endif()