cmake_minimum_required(VERSION 3.5.0)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(HAILORT_SERVER_SOURCES
    hailort_server.cpp
    ${HRPC_CPP_SOURCES}
    ${HRPC_PROTOCOL_CPP_SOURCES}
    ${HAILORT_SERVICE_DIR}/cng_buffer_pool.cpp
    ${DRIVER_OS_DIR}/driver_os_specific.cpp
    ${HAILORT_SRC_DIR}/vdma/pcie_session.cpp
    ${HAILORT_SRC_DIR}/vdma/memory/descriptor_list.cpp
    ${HAILORT_SRC_DIR}/vdma/memory/mapped_buffer.cpp
    ${HAILORT_SRC_DIR}/vdma/memory/dma_able_buffer.cpp
    ${HAILORT_SRC_DIR}/vdma/driver/hailort_driver.cpp
    ${HAILORT_SRC_DIR}/vdma/channel/interrupts_dispatcher.cpp
    ${HAILORT_SRC_DIR}/vdma/channel/transfer_launcher.cpp
    ${HAILORT_SRC_DIR}/vdma/channel/boundary_channel.cpp
    ${HAILORT_SRC_DIR}/vdma/channel/channels_group.cpp
    ${HAILORT_SRC_DIR}/vdma/channel/transfer_common.cpp
)

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
    hailort_common
)
