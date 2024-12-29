cmake_minimum_required(VERSION 3.5.0)

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/device.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/device_internal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/d2h_events_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/d2h_event_queue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/control.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/control_soc.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/control_protocol.cpp
)

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${SRC_FILES} PARENT_SCOPE)
