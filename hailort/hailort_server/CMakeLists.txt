cmake_minimum_required(VERSION 3.0.0)

include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/spdlog.cmake)
include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/readerwriterqueue.cmake)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(HAILORT_SERVER_SOURCES
    hailort_server.cpp
    ${HRPC_CPP_SOURCES}
    ${HRPC_PROTOCOL_CPP_SOURCES}
    ${HAILORT_COMMON_OS_DIR}/os_utils.cpp
    ${HAILORT_SERVICE_DIR}/cng_buffer_pool.cpp
    ${HAILORT_COMMON_DIR}/common/event_internal.cpp
    ${HAILO_FULL_OS_DIR}/event.cpp # TODO HRT-10681: move to common
    ${DRIVER_OS_DIR}/driver_os_specific.cpp
    ${HAILO_OS_DIR}/file_descriptor.cpp
    ${HAILO_OS_DIR}/mmap_buffer.cpp
    ${HAILORT_SRC_DIR}/vdma/pcie_session.cpp
    ${HAILORT_SRC_DIR}/vdma/memory/descriptor_list.cpp
    ${HAILORT_SRC_DIR}/vdma/memory/mapped_buffer.cpp
    ${HAILORT_SRC_DIR}/vdma/memory/dma_able_buffer.cpp
    ${HAILORT_SRC_DIR}/vdma/memory/vdma_edge_layer.cpp
    ${HAILORT_SRC_DIR}/vdma/driver/hailort_driver.cpp
    ${HAILORT_SRC_DIR}/vdma/channel/interrupts_dispatcher.cpp
    ${HAILORT_SRC_DIR}/vdma/channel/transfer_launcher.cpp
    ${HAILORT_SRC_DIR}/vdma/channel/boundary_channel.cpp
    ${HAILORT_SRC_DIR}/vdma/channel/channels_group.cpp
    ${HAILORT_SRC_DIR}/stream_common/transfer_common.cpp
)
if(WIN32)
    # hailort_driver.cpp in windows depends on string_conversion
    # dma_able_buffer.cpp in windows depends on virtual_alloc_guard
    set(HAILORT_SERVER_SOURCES ${HAILORT_SERVER_SOURCES}
        ${HAILORT_COMMON_OS_DIR}/string_conversion.cpp
        ${HAILO_FULL_OS_DIR}/virtual_alloc_guard.cpp)
endif()

add_executable(hailort_server ${HAILORT_SERVER_SOURCES})
target_include_directories(hailort_server PRIVATE
    ${HAILORT_SRC_DIR}
    ${COMMON_INC_DIR}
    ${DRIVER_INC_DIR}
)
target_compile_options(hailort_server PRIVATE ${HAILORT_COMPILE_OPTIONS})
set_property(TARGET hailort_server PROPERTY CXX_STANDARD 14)
set_property(TARGET hailort_server PROPERTY INSTALL_RPATH "$ORIGIN" "../lib/") # Link with a relative libhailort
target_link_libraries(hailort_server PRIVATE
    libhailort
    Threads::Threads
    rpc_proto
    spdlog::spdlog
    readerwriterqueue
)