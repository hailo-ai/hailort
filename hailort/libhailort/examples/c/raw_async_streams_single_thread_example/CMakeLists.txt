cmake_minimum_required(VERSION 3.5.0)

project(hailort-examples-c-raw-async-streams-single-thread)

find_package(HailoRT 5 REQUIRED)

SET_SOURCE_FILES_PROPERTIES(raw_async_streams_single_thread_example.c PROPERTIES LANGUAGE C)

add_executable(c_raw_async_streams_single_thread_example raw_async_streams_single_thread_example.c)
target_link_libraries(c_raw_async_streams_single_thread_example PRIVATE HailoRT::libhailort)
target_include_directories(c_raw_async_streams_single_thread_example PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../common")

if (WIN32)
    # Ensure Windows SDK knows the target architecture
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_compile_definitions(_AMD64_=1)
    else()
        add_compile_definitions(_X86_=1)
    endif()

    target_compile_options(c_raw_async_streams_single_thread_example PRIVATE
        /DNOMINMAX                  # NOMINMAX is required in order to avoid conflicts with std::min/std::max (otherwise Windows.h defines it's own)
        /wd4201 /wd4251
    )
endif()