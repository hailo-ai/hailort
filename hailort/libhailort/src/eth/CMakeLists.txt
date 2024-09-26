cmake_minimum_required(VERSION 3.5.0)

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/eth_device.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/eth_stream.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hcp_config_core_op.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/udp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network_rate_calculator.cpp
)

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${SRC_FILES} PARENT_SCOPE)
