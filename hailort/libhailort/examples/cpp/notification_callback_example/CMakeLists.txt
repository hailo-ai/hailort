cmake_minimum_required(VERSION 3.5.0)

find_package(HailoRT 4.23.0 EXACT REQUIRED)

add_executable(cpp_notification_callback_example notification_callback_example.cpp)
target_link_libraries(cpp_notification_callback_example PRIVATE HailoRT::libhailort)

if(WIN32)
    target_compile_options(cpp_notification_callback_example PRIVATE
        /DWIN32_LEAN_AND_MEAN
        /DNOMINMAX                  # NOMINMAX is required in order to play nice with std::min/std::max (otherwise Windows.h defines it's own)
        /wd4201 /wd4251
    )
endif()

set_target_properties(cpp_notification_callback_example PROPERTIES CXX_STANDARD 14)
