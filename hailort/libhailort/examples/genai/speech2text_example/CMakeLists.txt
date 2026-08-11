cmake_minimum_required(VERSION 3.5.0)

project(hailort-examples-genai-chat)

find_package(HailoRT 5 REQUIRED)

add_executable(speech2text_example speech2text_example.cpp)

target_link_libraries(speech2text_example PRIVATE HailoRT::libhailort)

if (WIN32)
    target_compile_options(speech2text_example PRIVATE
        /wd4201 /wd4251
    )
endif()

set_target_properties(speech2text_example PROPERTIES CXX_STANDARD 17)