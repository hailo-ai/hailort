cmake_minimum_required(VERSION 3.5.0)

project(hailort-examples-cpp-async-infer-dma-buffer)

if (NOT UNIX)
    message(FATAL_ERROR "The Async Infer using DMA Buffer Example is only available on UNIX systems")
endif()

find_package(HailoRT 5 REQUIRED)

add_executable(cpp_async_infer_dma_buffer_example async_infer_dma_buffer_example.cpp)
target_link_libraries(cpp_async_infer_dma_buffer_example PRIVATE HailoRT::libhailort)

set_target_properties(cpp_async_infer_dma_buffer_example PROPERTIES CXX_STANDARD 17)
