cmake_minimum_required(VERSION 3.5.0)

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/tracer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scheduler_profiler_handler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/monitor_handler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/perfetto_handler.cpp
)

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${SRC_FILES} PARENT_SCOPE)
