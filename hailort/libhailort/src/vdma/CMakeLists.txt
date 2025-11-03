cmake_minimum_required(VERSION 3.5.0)

if(WIN32)
    set(DRIVER_OS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/driver/os/windows")
elseif(UNIX)
    if (CMAKE_SYSTEM_NAME STREQUAL QNX)
        set(DRIVER_OS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/driver/os/posix/qnx")
    else()
        set(DRIVER_OS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/driver/os/posix/linux")
    endif()
else()
    message(FATAL_ERROR "Unexpeced platform target, stopping build")
endif()
set(DRIVER_OS_DIR ${DRIVER_OS_DIR} PARENT_SCOPE)

set(DRIVER_SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/driver/hailort_driver.cpp
    ${DRIVER_OS_DIR}/driver_os_specific.cpp
)

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/vdma_device.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vdma_config_core_op.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vdma_config_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vdma_stream.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/circular_stream_buffer_pool.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/pcie_session.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/dma_mapped_buffer.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/pcie/pcie_device.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/integrated/integrated_device.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/channel/boundary_channel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/channel/channels_group.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/channel/interrupts_dispatcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/channel/transfer_launcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/channel/transfer_common.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/memory/descriptor_list.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/vdma_edge_layer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/mapped_buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/dma_able_buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/sg_edge_layer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/multi_sg_edge_layer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/continuous_edge_layer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/buffer_requirements.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/vdma_buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/continuous_buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/sg_buffer.cpp

    ${DRIVER_SRC_FILES}
)

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${SRC_FILES} PARENT_SCOPE)

# Export DRIVER_SRC_FILES as HALIO_DRIVER_SRC_FILES to parent scope
set(HAILO_DRIVER_SRC_FILES ${DRIVER_SRC_FILES} PARENT_SCOPE)