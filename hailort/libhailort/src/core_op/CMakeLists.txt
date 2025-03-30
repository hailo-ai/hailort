cmake_minimum_required(VERSION 3.5.0)

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/core_op.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/resource_manager/resource_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/resource_manager/resource_manager_builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/resource_manager/config_buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/resource_manager/cache_buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/resource_manager/channel_allocator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/resource_manager/action_list_buffer_builder/action_list_buffer_builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/resource_manager/periph_calculator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/resource_manager/internal_buffer_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/resource_manager/internal_buffer_planner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/resource_manager/cache_manager.cpp
)

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${SRC_FILES} PARENT_SCOPE)
