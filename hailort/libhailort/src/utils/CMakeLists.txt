cmake_minimum_required(VERSION 3.0.0)

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/hailort_common.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hailort_logger.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/buffer_storage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sensor_config_utils.cpp
)

if(HAILO_BUILD_PROFILER)
    add_subdirectory(profiler)
endif()

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${SRC_FILES} PARENT_SCOPE)
