cmake_minimum_required(VERSION 3.5.0)

set(RAW_CONN_IMPL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/raw_connection_internal")

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/rpc_connection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hailo_session.cpp
    ${RAW_CONN_IMPL_DIR}/pcie/hailo_session_internal.cpp
    ${RAW_CONN_IMPL_DIR}/socket/hailo_session_internal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/connection_context.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/server.cpp
)

set(HRPC_CPP_SOURCES ${SRC_FILES} PARENT_SCOPE)