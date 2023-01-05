cmake_minimum_required(VERSION 3.0.0)

if(WIN32)
    set(HAILO_OS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/windows")
    set(HAILO_FULL_OS_DIR ${HAILO_OS_DIR})
elseif(UNIX)
    set(HAILO_OS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/posix")
    if (CMAKE_SYSTEM_NAME STREQUAL QNX)
        set(HAILO_FULL_OS_DIR ${HAILO_OS_DIR}/qnx)
    else()
        set(HAILO_FULL_OS_DIR ${HAILO_OS_DIR}/unix)
    endif()
else()
    message(FATAL_ERROR "Unexpeced platform target, stopping build")
endif()
# This is needed for hailortcli CMakeLists. PARENT_SCOPE is added in a different set because
# adding it to the definitions of HAILO_OS_DIR above will make the variable empty in the current scope.
set(HAILO_OS_DIR ${HAILO_OS_DIR} PARENT_SCOPE)
set(HAILO_FULL_OS_DIR ${HAILO_FULL_OS_DIR} PARENT_SCOPE)


set(files
    ${HAILO_OS_DIR}/microsec_timer.cpp
    ${HAILO_OS_DIR}/file_descriptor.cpp
    ${HAILO_OS_DIR}/mmap_buffer.cpp
    ${HAILO_OS_DIR}/hailort_driver.cpp
    ${HAILO_FULL_OS_DIR}/event.cpp
    ${HAILO_FULL_OS_DIR}/driver_scan.cpp
)

set(HAILORT_CPP_OS_SOURCES ${files} PARENT_SCOPE)
