cmake_minimum_required(VERSION 3.5.0)

find_package(HailoRT 5.0.0 EXACT REQUIRED)

add_executable(image_generation_example image_generation_example.cpp)
target_link_libraries(image_generation_example PRIVATE HailoRT::libhailort)

if(WIN32)
    target_compile_options(image_generation_example PRIVATE
        /DWIN32_LEAN_AND_MEAN
        /DNOMINMAX                  # NOMINMAX is required in order to play nice with std::min/std::max (otherwise Windows.h defines it's own)
        /wd4201 /wd4251
    )
endif()

set_target_properties(image_generation_example PROPERTIES CXX_STANDARD 14)