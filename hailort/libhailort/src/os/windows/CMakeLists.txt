cmake_minimum_required(VERSION 3.0.0)

set(files
    ${CMAKE_CURRENT_SOURCE_DIR}/microsec_timer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/file_descriptor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mmap_buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hailort_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/event.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/driver_scan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/virtual_alloc_guard.cpp
)

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${files} PARENT_SCOPE)
