cmake_minimum_required(VERSION 3.5.0)

project(hailort-examples-c-notification-callback)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(HailoRT 5 REQUIRED)

SET_SOURCE_FILES_PROPERTIES(notification_callback_example.c PROPERTIES LANGUAGE C)

add_executable(c_notification_callback_example notification_callback_example.c)
target_link_libraries(c_notification_callback_example PRIVATE HailoRT::libhailort Threads::Threads)
target_include_directories(c_notification_callback_example PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../common")

if (WIN32)
    # Ensure Windows SDK knows the target architecture
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_compile_definitions(_AMD64_=1)
    else()
        add_compile_definitions(_X86_=1)
    endif()

    target_compile_options(c_notification_callback_example PRIVATE
        /DNOMINMAX                  # NOMINMAX is required in order to avoid conflicts with std::min/std::max (otherwise Windows.h defines it's own)
        /wd4201 /wd4251
    )
endif()