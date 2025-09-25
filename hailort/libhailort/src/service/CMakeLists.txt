cmake_minimum_required(VERSION 3.5.0)

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/buffer_pool_per_stream.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hailort_rpc_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network_group_client.cpp
)

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${SRC_FILES} PARENT_SCOPE)
