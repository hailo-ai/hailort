cmake_minimum_required(VERSION 3.5.0)

project(hailort-examples-genai-vlm)

find_package(HailoRT 5 REQUIRED)

add_executable(vlm_example vlm_example.cpp)

target_link_libraries(vlm_example PRIVATE HailoRT::libhailort)

if (WIN32)
    # Ensure Windows SDK knows the target architecture
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_compile_definitions(_AMD64_=1)
    else()
        add_compile_definitions(_X86_=1)
    endif()

    target_compile_options(vlm_example PRIVATE
        /DNOMINMAX                  # NOMINMAX is required in order to avoid conflicts with std::min/std::max (otherwise Windows.h defines it's own)
        /wd4201 /wd4251
    )
endif()

set_target_properties(vlm_example PROPERTIES CXX_STANDARD 17)