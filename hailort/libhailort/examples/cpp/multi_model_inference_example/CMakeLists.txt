cmake_minimum_required(VERSION 3.5.0)

project(hailort-examples-cpp-multi-model-inference)

find_package(HailoRT 5 REQUIRED)

add_executable(cpp_multi_model_inference_example multi_model_inference_example.cpp)
target_link_libraries(cpp_multi_model_inference_example PRIVATE HailoRT::libhailort)

if (WIN32)
    # Ensure Windows SDK knows the target architecture
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_compile_definitions(_AMD64_=1)
    else()
        add_compile_definitions(_X86_=1)
    endif()

    target_compile_options(cpp_multi_model_inference_example PRIVATE
        /DNOMINMAX                  # NOMINMAX is required in order to avoid conflicts with std::min/std::max (otherwise Windows.h defines it's own)
        /wd4201 /wd4251
    )
endif()

set_target_properties(cpp_multi_model_inference_example PROPERTIES CXX_STANDARD 17)
