cmake_minimum_required(VERSION 3.5.0)

set(files
    ${CMAKE_CURRENT_SOURCE_DIR}/event.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dma_buffer_utils.cpp
)

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${files} PARENT_SCOPE)
