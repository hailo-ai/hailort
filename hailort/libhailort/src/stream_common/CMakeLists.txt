cmake_minimum_required(VERSION 3.5.0)

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/stream.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stream_internal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/async_stream_base.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/nms_stream.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/remote_process_stream.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/queued_stream_buffer_pool.cpp
)

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${SRC_FILES} PARENT_SCOPE)
