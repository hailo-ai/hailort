cmake_minimum_required(VERSION 3.5.0)

if (CMAKE_SYSTEM_NAME STREQUAL QNX)
    add_subdirectory(qnx)
else()
    add_subdirectory(linux)
endif()

set(HAILORT_COMMON_CPP_SOURCES ${HAILORT_COMMON_CPP_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/ethernet_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/filesystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/socket.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/process.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/os_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/file_descriptor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mmap_buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shared_memory_buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/traffic_control.cpp

    PARENT_SCOPE
)
