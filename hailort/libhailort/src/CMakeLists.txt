cmake_minimum_required(VERSION 3.0.0)

find_package(Threads REQUIRED)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/common_compiler_options.cmake)

FUNCTION(relative_to_absolute_paths output)
    SET(listVar "")
    FOREACH(rel_path ${ARGN})
        get_filename_component(abs_path "${rel_path}" ABSOLUTE)
        LIST(APPEND listVar ${abs_path})
    ENDFOREACH(rel_path)
    SET(${output} "${listVar}" PARENT_SCOPE)
ENDFUNCTION(relative_to_absolute_paths)

add_subdirectory(os)

set(HAILORT_CPP_SOURCES
    device.cpp
    device_internal.cpp
    control.cpp
    stream.cpp
    stream_internal.cpp
    transform.cpp
    buffer.cpp
    network_rate_calculator.cpp
    hailort_logger.cpp
    hailort.cpp
    hailort_common.cpp
    sensor_config_utils.cpp
    pipeline.cpp

    eth_device.cpp
    eth_stream.cpp
    udp.cpp

    hef.cpp
    context_switch/hef_metadata.cpp
    context_switch/hcp_config_manager.cpp
    context_switch/hcp_config_network_group.cpp
    context_switch/hcp_config_activated_network_group.cpp
    context_switch/vdma_config_manager.cpp
    context_switch/vdma_config_network_group.cpp
    context_switch/vdma_config_activated_network_group.cpp
    context_switch/network_group.cpp
    context_switch/network_group_wrapper.cpp
    context_switch/resource_manager.cpp
    context_switch/pipeline_multiplexer.cpp
    channel_allocator.cpp
    inter_context_buffer.cpp
    ddr_channels_pair.cpp
    config_buffer.cpp
    d2h_events_parser.cpp
    mipi_stream.cpp

    hlpcie.cpp
    vdma_channel.cpp
    vdma_descriptor_list.cpp
    vdma_device.cpp
    vdma_stream.cpp

    vdma/vdma_mapped_buffer_impl.cpp
    vdma/mapped_buffer.cpp
    vdma/sg_buffer.cpp
    vdma/continuous_buffer.cpp
    vdma/vdma_buffer.cpp

    pcie_device.cpp
    pcie_stream.cpp

    core_device.cpp
    core_stream.cpp

    vdevice.cpp
    vdevice_stream.cpp
    vdevice_stream_wrapper.cpp

    control_protocol.cpp

    vstream.cpp
    inference_pipeline.cpp

    network_group_scheduler.cpp
)

set(common_dir "${PROJECT_SOURCE_DIR}/common/src")
set(COMMON_C_SOURCES
    ${common_dir}/firmware_status.c
    ${common_dir}/md5.c
    ${common_dir}/firmware_header_utils.c
)

# Global var to be used by test projects to compile hailort sources
relative_to_absolute_paths(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES})
relative_to_absolute_paths(C_OS_SOURCES ${C_OS_SOURCES})
relative_to_absolute_paths(COMMON_C_SOURCES ${COMMON_C_SOURCES})
relative_to_absolute_paths(HAILO_OS_DIR ${HAILO_OS_DIR})
relative_to_absolute_paths(HAILO_FULL_OS_DIR ${HAILO_FULL_OS_DIR})
set(HAILO_OS_DIR ${HAILO_OS_DIR} CACHE INTERNAL "Absolute path of os-dir")
set(HAILO_FULL_OS_DIR ${HAILO_FULL_OS_DIR} CACHE INTERNAL "Absolute Full path of os-dir")
set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} CACHE INTERNAL "Absolute paths of hailort's cpp source files")
set(HAILORT_CPP_OS_SOURCES ${HAILORT_CPP_OS_SOURCES} CACHE INTERNAL "Absolute paths of os-related source files")
set(COMMON_C_SOURCES ${COMMON_C_SOURCES} CACHE INTERNAL "Absolute paths of common source files")
set(HAILORT_SRCS_ABS ${HAILORT_CPP_SOURCES} ${HAILORT_CPP_OS_SOURCES} ${HAILORT_COMMON_CPP_SOURCES} ${COMMON_C_SOURCES} CACHE INTERNAL "All absolute paths of hailort's source files")

SET_SOURCE_FILES_PROPERTIES(${C_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(libhailort SHARED ${HAILORT_SRCS_ABS})

# Include libraries
if(WIN32)
    target_link_libraries(libhailort PRIVATE
        Ws2_32
        Iphlpapi
        Shlwapi
    )
    set_property(TARGET libhailort PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
else()
    target_link_libraries(libhailort PRIVATE
        m # libmath
        atomic
    )
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    # Hack to support cross-compilation - https://stackoverflow.com/a/49086560
    set(THREADS_PTHREAD_ARG "0" CACHE STRING "Result from TRY_RUN" FORCE)
endif()

target_link_libraries(libhailort PRIVATE Threads::Threads)
target_link_libraries(libhailort PRIVATE hef_proto)
target_link_libraries(libhailort PRIVATE spdlog::spdlog)
target_link_libraries(libhailort PRIVATE readerwriterqueue)
target_link_libraries(libhailort PRIVATE microprofile)
if(CMAKE_SYSTEM_NAME STREQUAL QNX)
    target_link_libraries(libhailort PRIVATE pevents pci)
endif()

set(HAILORT_PUBLIC_HEADERS
    ${HAILORT_INC_DIR}/hailo/hailort.h
    ${HAILORT_INC_DIR}/hailo/platform.h

    ${HAILORT_INC_DIR}/hailo/buffer.hpp
    ${HAILORT_INC_DIR}/hailo/device.hpp
    ${HAILORT_INC_DIR}/hailo/event.hpp
    ${HAILORT_INC_DIR}/hailo/expected.hpp
    ${HAILORT_INC_DIR}/hailo/hailort_common.hpp
    ${HAILORT_INC_DIR}/hailo/hailort.hpp
    ${HAILORT_INC_DIR}/hailo/hef.hpp
    ${HAILORT_INC_DIR}/hailo/network_group.hpp
    ${HAILORT_INC_DIR}/hailo/stream.hpp
    ${HAILORT_INC_DIR}/hailo/transform.hpp
    ${HAILORT_INC_DIR}/hailo/vstream.hpp
    ${HAILORT_INC_DIR}/hailo/inference_pipeline.hpp
    ${HAILORT_INC_DIR}/hailo/runtime_statistics.hpp
    ${HAILORT_INC_DIR}/hailo/network_rate_calculator.hpp
    ${HAILORT_INC_DIR}/hailo/vdevice.hpp
    ${HAILORT_INC_DIR}/hailo/quantization.hpp
)

set_target_properties(libhailort PROPERTIES
    PUBLIC_HEADER "${HAILORT_PUBLIC_HEADERS}"
    PREFIX ""
    VERSION ${HAILORT_MAJOR_VERSION}.${HAILORT_MINOR_VERSION}.${HAILORT_REVISION_VERSION}
    # SOVERSION ${HAILORT_MAJOR_VERSION}

    CXX_STANDARD              14
    CXX_STANDARD_REQUIRED     YES
    CXX_EXTENSIONS            NO
    C_VISIBILITY_PRESET       hidden
    CXX_VISIBILITY_PRESET     hidden
    # VISIBILITY_INLINES_HIDDEN YES
)

target_compile_options(libhailort PRIVATE ${HAILORT_COMPILE_OPTIONS})
disable_exceptions(libhailort)
exclude_archive_libs_symbols(libhailort)

target_include_directories(libhailort
    PUBLIC
    $<BUILD_INTERFACE:${HAILORT_INC_DIR}>
    $<BUILD_INTERFACE:${HAILORT_COMMON_DIR}>
    PRIVATE
    $<BUILD_INTERFACE:${HAILORT_SRC_DIR}>
    $<BUILD_INTERFACE:${COMMON_INC_DIR}>
    $<BUILD_INTERFACE:${DRIVER_INC_DIR}>
)

target_compile_definitions(libhailort PUBLIC
    -DHAILORT_MAJOR_VERSION=${HAILORT_MAJOR_VERSION}
    -DHAILORT_MINOR_VERSION=${HAILORT_MINOR_VERSION}
    -DHAILORT_REVISION_VERSION=${HAILORT_REVISION_VERSION}
)

install(TARGETS libhailort
    EXPORT HailoRTTargets
    CONFIGURATIONS Release
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/hailo"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    install(CODE "execute_process(COMMAND ldconfig)")
endif()

# Export libhailort
set(CMAKE_SCRIPTS_DIR ${CMAKE_CURRENT_BINARY_DIR})
configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/Config.cmake.in
  "${CMAKE_SCRIPTS_DIR}/HailoRTConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HailoRT
)
write_basic_package_version_file(
  "${CMAKE_SCRIPTS_DIR}/HailoRTConfigVersion.cmake"
  VERSION "${HAILORT_MAJOR_VERSION}.${HAILORT_MINOR_VERSION}.${HAILORT_REVISION_VERSION}"
  COMPATIBILITY ExactVersion
)

# Support builds without installation
set(HailoRT_DIR "${CMAKE_SCRIPTS_DIR}" PARENT_SCOPE)

# Package installation
install(FILES
        "${CMAKE_SCRIPTS_DIR}/HailoRTConfig.cmake"
        "${CMAKE_SCRIPTS_DIR}/HailoRTConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HailoRT
    COMPONENT libhailort
)
install(EXPORT HailoRTTargets
    FILE HailoRTTargets.cmake
    NAMESPACE HailoRT::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HailoRT
    COMPONENT libhailort
)
