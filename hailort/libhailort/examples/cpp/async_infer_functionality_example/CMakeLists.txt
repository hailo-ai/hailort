cmake_minimum_required(VERSION 3.0.0)

find_package(HailoRT 4.16.1 EXACT REQUIRED)

add_executable(cpp_async_infer_functionality_example async_infer_functionality_example.cpp)
target_link_libraries(cpp_async_infer_functionality_example PRIVATE HailoRT::libhailort)

if(WIN32)
    target_compile_options(cpp_async_infer_functionality_example PRIVATE
        /DWIN32_LEAN_AND_MEAN
        /DNOMINMAX                  # NOMINMAX is required in order to play nice with std::min/std::max (otherwise Windows.h defines it's own)
        /wd4201 /wd4251
    )
endif()

set_target_properties(cpp_async_infer_functionality_example PROPERTIES CXX_STANDARD 14)
