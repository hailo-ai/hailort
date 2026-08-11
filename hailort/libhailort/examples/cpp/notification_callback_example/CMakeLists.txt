cmake_minimum_required(VERSION 3.5.0)

project(hailort-examples-cpp-notification-callback)

find_package(HailoRT 5 REQUIRED)

add_executable(cpp_notification_callback_example notification_callback_example.cpp)
target_link_libraries(cpp_notification_callback_example PRIVATE HailoRT::libhailort)

if (WIN32)
    target_compile_options(cpp_notification_callback_example PRIVATE
        /wd4201 /wd4251
    )
endif()

set_target_properties(cpp_notification_callback_example PROPERTIES CXX_STANDARD 17)
