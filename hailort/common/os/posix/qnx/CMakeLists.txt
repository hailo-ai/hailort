cmake_minimum_required(VERSION 3.5.0)

set(HAILORT_COMMON_CPP_SOURCES ${HAILORT_COMMON_CPP_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/event_os_specific.cpp

    PARENT_SCOPE
)
