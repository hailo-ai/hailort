cmake_minimum_required(VERSION 3.5.0)

if(WIN32)
    set(HAILO_OS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/windows")
    set(HAILO_FULL_OS_DIR ${HAILO_OS_DIR})
elseif(UNIX)
    set(HAILO_OS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/posix")
    if (CMAKE_SYSTEM_NAME STREQUAL QNX)
        set(HAILO_FULL_OS_DIR ${HAILO_OS_DIR}/qnx)
    else()
        set(HAILO_FULL_OS_DIR ${HAILO_OS_DIR}/linux)
    endif()
else()
    message(FATAL_ERROR "Unexpeced platform target, stopping build")
endif()
# This is needed for hailortcli CMakeLists. PARENT_SCOPE is added in a different set because
# adding it to the definitions of HAILO_OS_DIR above will make the variable empty in the current scope.
set(HAILO_OS_DIR ${HAILO_OS_DIR} PARENT_SCOPE)
set(HAILO_FULL_OS_DIR ${HAILO_FULL_OS_DIR} PARENT_SCOPE)


if(WIN32)
    add_subdirectory(windows)
elseif(UNIX)
    add_subdirectory(posix)
else()
    message(FATAL_ERROR "Unexpeced platform target, stopping build")
endif()

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${SRC_FILES} PARENT_SCOPE)