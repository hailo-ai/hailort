cmake_minimum_required(VERSION 3.5.0)

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/hef.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_op_metadata.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/context_switch_actions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/memory_requirements_calculator.cpp
)

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${SRC_FILES} PARENT_SCOPE)
