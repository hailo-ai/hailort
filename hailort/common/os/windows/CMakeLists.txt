cmake_minimum_required(VERSION 3.5.0)

set(HAILORT_COMMON_CPP_SOURCES ${HAILORT_COMMON_CPP_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/filesystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/socket.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/process.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/os_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/file_descriptor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mmap_buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shared_memory_buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/string_conversion.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/virtual_alloc_guard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/named_mutex_guard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/event_os_specific.cpp

    PARENT_SCOPE
)
