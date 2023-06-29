cmake_minimum_required(VERSION 3.0.0)

if(WIN32)
    set(HAILORT_COMMON_OS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/os/windows")
elseif(UNIX)
    set(HAILORT_COMMON_OS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/os/posix")
else()
    message(FATAL_ERROR "Unexpeced host, stopping build")
endif()
set(HAILORT_COMMON_OS_DIR ${HAILORT_COMMON_OS_DIR} PARENT_SCOPE)

set(SRC_FILES
    ${HAILORT_COMMON_OS_DIR}/ethernet_utils.cpp
    ${HAILORT_COMMON_OS_DIR}/filesystem.cpp
    ${HAILORT_COMMON_OS_DIR}/socket.cpp
    ${HAILORT_COMMON_OS_DIR}/process.cpp
    ${HAILORT_COMMON_OS_DIR}/os_utils.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/barrier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/file_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/string_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/event_internal.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/device_measurements.cpp
)

if(WIN32)
    # Windows only modules:
    set(SRC_FILES ${SRC_FILES}
        ${HAILORT_COMMON_OS_DIR}/string_conversion.cpp
    )
elseif(UNIX)
    # Unix only modules
    set(SRC_FILES ${SRC_FILES}
        ${HAILORT_COMMON_OS_DIR}/traffic_control.cpp
    )
endif()

set(HAILORT_COMMON_CPP_SOURCES ${SRC_FILES} PARENT_SCOPE)