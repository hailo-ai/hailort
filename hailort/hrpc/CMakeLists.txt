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

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
add_library(hailo_rpc STATIC EXCLUDE_FROM_ALL ${SRC_FILES})
target_include_directories(hailo_rpc PRIVATE
    ${HAILORT_SRC_DIR}
    ${COMMON_INC_DIR}
    ${DRIVER_INC_DIR}
)
target_compile_options(hailo_rpc PRIVATE ${HAILORT_COMPILE_OPTIONS})
set_target_properties(hailo_rpc PROPERTIES CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
target_link_libraries(hailo_rpc PRIVATE
    Threads::Threads
    rpc_proto
    hailort_common
)
