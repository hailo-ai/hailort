cmake_minimum_required(VERSION 3.5.0)

set(RAW_CONN_IMPL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/session_internal")

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/rpc_connection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hailo_session.cpp
    ${RAW_CONN_IMPL_DIR}/pcie_session_internal.cpp
    ${RAW_CONN_IMPL_DIR}/async_actions_thread.cpp
    ${RAW_CONN_IMPL_DIR}/eth_session.cpp
    ${RAW_CONN_IMPL_DIR}/usb/usb_session_common.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/connection_context.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/server.cpp
)
set(HRPC_CPP_SOURCES ${SRC_FILES} PARENT_SCOPE)

set(CLIENT_SRC_FILES ${RAW_CONN_IMPL_DIR}/usb/usb_device/usb_session_noimpl_device.cpp)
if (HAILO_BUILD_USB)
    list(APPEND CLIENT_SRC_FILES
        ${RAW_CONN_IMPL_DIR}/usb/usb_host/usb_host_side.cpp
        ${RAW_CONN_IMPL_DIR}/usb/usb_host/libusb_event_thread.cpp
        ${RAW_CONN_IMPL_DIR}/usb/usb_host/usb_transfer_queue.cpp
    )
else()
    list(APPEND CLIENT_SRC_FILES ${RAW_CONN_IMPL_DIR}/usb/usb_host/usb_session_noimpl_host.cpp)
endif()
set(HRPC_CLIENT_CPP_SOURCES ${CLIENT_SRC_FILES} PARENT_SCOPE)

set(SERVER_SRC_FILES
    ${RAW_CONN_IMPL_DIR}/usb/usb_device/usb_device_side.cpp
    ${RAW_CONN_IMPL_DIR}/usb/usb_host/usb_session_noimpl_host.cpp
    ${RAW_CONN_IMPL_DIR}/usb/usb_device/usb_control_listener.cpp
    ${RAW_CONN_IMPL_DIR}/usb/usb_device/usb_event_loop.cpp
    ${RAW_CONN_IMPL_DIR}/usb/usb_device/aio_context.cpp
)
set(HRPC_SERVER_CPP_SOURCES ${SERVER_SRC_FILES} PARENT_SCOPE)

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
