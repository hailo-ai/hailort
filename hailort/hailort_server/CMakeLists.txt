cmake_minimum_required(VERSION 3.5.0)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(HAILORT_SERVER_SOURCES
    hailort_server.cpp
    cng_buffer_pool.cpp
    ${HRPC_CPP_SOURCES}
    ${HRPC_PROTOCOL_CPP_SOURCES}
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
    ${HAILORT_SRC_DIR}/vdma/memory/continuous_buffer.cpp
    ${HAILORT_SRC_DIR}/utils/pool_allocator.cpp
    ${HAILORT_SRC_DIR}/utils/buffer_storage.cpp
)

if (HAILO_BUILD_GENAI_SERVER)
    if (WIN32)
        message(FATAL_ERROR "Compilation of GenAI Server is not supported on Windows")
    endif()

    include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/eigen.cmake)
    include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/tokenizers.cmake)
    include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/json.cmake)

    add_subdirectory(genai)
    list(APPEND HAILORT_SERVER_SOURCES
        ${GENAI_SCHEME_CPP_SOURCES}
        ${HAILORT_GENAI_SOURCES}
    )
endif()

add_executable(hailort_server ${HAILORT_SERVER_SOURCES} server_main.cpp)

target_include_directories(hailort_server PRIVATE
    ${HAILORT_SRC_DIR}
    ${COMMON_INC_DIR}
    ${DRIVER_INC_DIR}
)

target_compile_options(hailort_server PRIVATE ${HAILORT_COMPILE_OPTIONS})
set_property(TARGET hailort_server PROPERTY CXX_STANDARD 17)
set_property(TARGET hailort_server PROPERTY INSTALL_RPATH "$ORIGIN" "../lib/") # Link with a relative libhailort

target_link_libraries(hailort_server PRIVATE
    libhailort
    Threads::Threads
    rpc_proto
    hailort_common
)

if (HAILO_BUILD_GENAI_SERVER)
    target_include_directories(hailort_server PRIVATE
        ${HAILORT_GENAI_INCLUDE_DIR}
        ${HAILORT_TOKENIZER_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_definitions(hailort_server PRIVATE HAILO_GENAI_SERVER)

    target_link_libraries(hailort_server PRIVATE
        genai_scheme_proto
        tokenizers_cpp
        eigen
        nlohmann_json
    )
endif()
