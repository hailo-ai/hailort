cmake_minimum_required(VERSION 3.5.0)

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/hailort_common.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hailort_logger.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/buffer_storage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sensor_config_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/soc_utils/partial_cluster_reader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/measurement_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/query_stats_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logger_fetcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pool_allocator.cpp
)

add_subdirectory(profiler)

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${SRC_FILES} PARENT_SCOPE)
