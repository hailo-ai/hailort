cmake_minimum_required(VERSION 3.0.0)

project(hailort-examples)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_LIST_DIR}/cmake/")
find_package(HailoRT)
find_package(Threads REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)

add_library(example_base INTERFACE)
target_link_libraries(example_base INTERFACE HailoRT::libhailort Threads::Threads)

if(WIN32)
    target_compile_options(example_base INTERFACE /W4 /WX /DWIN32_LEAN_AND_MEAN /DNOMINMAX /wd4201 /wd4251)
else()
    target_compile_options(example_base INTERFACE -Werror -Wall -Wextra -Wconversion -O3 -DNDEBUG) # TODO support debug/release builds
endif()

add_subdirectory(cpp)
add_subdirectory(c)

add_custom_target(hailort_examples DEPENDS ${EXAMPLES_C_TARGETS} ${EXAMPLES_CPP_TARGETS})