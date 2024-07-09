cmake_minimum_required(VERSION 3.0.0)

set(HRPC_IMPL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/os")
if(WIN32)
    set(HRPC_OS_DIR "${HRPC_IMPL_DIR}/windows")
elseif(UNIX)
    set(HRPC_OS_DIR "${HRPC_IMPL_DIR}/posix")
else()
    message(FATAL_ERROR "Unexpeced host, stopping build")
endif()

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/rpc_connection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/raw_connection.cpp
    ${HRPC_IMPL_DIR}/pcie/raw_connection_internal.cpp
    ${HRPC_OS_DIR}/raw_connection_internal.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/server.cpp
)

set(HRPC_CPP_SOURCES ${SRC_FILES} PARENT_SCOPE)