cmake_minimum_required(VERSION 3.5.0)

set(scheduler_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/scheduler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ddim.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/euler_discrete.cpp
)
set(TEXT2IMAGE_SCHEDULERS_SOURCES ${scheduler_sources} PARENT_SCOPE)