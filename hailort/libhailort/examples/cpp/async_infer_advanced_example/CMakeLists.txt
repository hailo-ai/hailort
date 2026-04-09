cmake_minimum_required(VERSION 3.5.0)

project(hailort-examples-cpp-async-infer-advanced)

find_package(HailoRT 5 REQUIRED)

add_executable(cpp_async_infer_advanced_example async_infer_advanced_example.cpp)
target_link_libraries(cpp_async_infer_advanced_example PRIVATE HailoRT::libhailort)

if (WIN32)
    target_compile_options(cpp_async_infer_advanced_example PRIVATE
        /wd4201 /wd4251
    )
endif()

set_target_properties(cpp_async_infer_advanced_example PROPERTIES CXX_STANDARD 17)
