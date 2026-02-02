cmake_minimum_required(VERSION 3.5.0)

project(hailort-examples-genai-chat-raw-prompt)

find_package(HailoRT 5 REQUIRED)

add_executable(chat_raw_prompt_example chat_raw_prompt.cpp)

target_link_libraries(chat_raw_prompt_example PRIVATE HailoRT::libhailort)

if (WIN32)
    # Ensure Windows SDK knows the target architecture
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_compile_definitions(_AMD64_=1)
    else()
        add_compile_definitions(_X86_=1)
    endif()

    target_compile_options(chat_raw_prompt_example PRIVATE
        /DNOMINMAX                  # NOMINMAX is required in order to avoid conflicts with std::min/std::max (otherwise Windows.h defines it's own)
        /wd4201 /wd4251
    )
endif()

set_target_properties(chat_raw_prompt_example PROPERTIES CXX_STANDARD 17)