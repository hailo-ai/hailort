cmake_minimum_required(VERSION 3.0.0)

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/vdma_device.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vdma_config_core_op.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vdma_config_activated_core_op.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vdma_config_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vdma_stream.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vdma_stream_base.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vdma_async_stream.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/pcie/pcie_device.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/integrated/integrated_device.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/channel/channel_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/channel/channel_base.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/channel/buffered_channel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/channel/boundary_channel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/channel/async_channel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/channel/interrupts_dispatcher.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/memory/descriptor_list.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/vdma_buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/dma_mapped_buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/mapped_buffer_impl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/mapped_buffer_factory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/sg_buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/continuous_buffer.cpp
)

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${SRC_FILES} PARENT_SCOPE)
