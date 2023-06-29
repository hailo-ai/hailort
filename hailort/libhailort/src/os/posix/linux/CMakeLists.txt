cmake_minimum_required(VERSION 3.0.0)

set(files
    ${CMAKE_CURRENT_SOURCE_DIR}/driver_scan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/event.cpp
)

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${files} PARENT_SCOPE)
