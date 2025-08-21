cmake_minimum_required(VERSION 3.5.0)

project(hailort-examples-genai-chat)

find_package(HailoRT 5 REQUIRED)

add_executable(chat_example chat.cpp)

target_link_libraries(chat_example PRIVATE HailoRT::libhailort)

if(WIN32)
    target_compile_options(chat_example PRIVATE
        /DWIN32_LEAN_AND_MEAN
        /DNOMINMAX                  # NOMINMAX is required in order to play nice with std::min/std::max (otherwise Windows.h defines it's own)
        /wd4201 /wd4251
    )
endif()

set_target_properties(chat_example PROPERTIES CXX_STANDARD 17)