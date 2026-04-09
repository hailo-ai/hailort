cmake_minimum_required(VERSION 3.5.0)

project(hailort-examples-genai-chat-raw-prompt)

find_package(HailoRT 5 REQUIRED)

add_executable(chat_raw_prompt_example chat_raw_prompt.cpp)

target_link_libraries(chat_raw_prompt_example PRIVATE HailoRT::libhailort)

if (WIN32)
    target_compile_options(chat_raw_prompt_example PRIVATE
        /wd4201 /wd4251
    )
endif()

set_target_properties(chat_raw_prompt_example PROPERTIES CXX_STANDARD 17)