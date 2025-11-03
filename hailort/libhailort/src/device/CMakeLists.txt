cmake_minimum_required(VERSION 3.5.0)

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/device_hrpc_client.cpp
)

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${SRC_FILES} PARENT_SCOPE)
