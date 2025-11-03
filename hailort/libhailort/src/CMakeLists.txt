cmake_minimum_required(VERSION 3.5.0)

option(HAILO_BUILD_CLIENT_TOKENIZER "Build with client-side tokenization support to optimize memory usage on device. Enables running large models on small-memory devices by moving tokenization to host. Requires cargo and rustup to be installed" OFF)

find_package(Threads REQUIRED)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/common_compiler_options.cmake)
include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/eigen.cmake)
include(genai/llm/minja_wrapper/minja_wrapper.cmake)

# Include tokenizers when client tokenizer is enabled
if(HAILO_BUILD_CLIENT_TOKENIZER)
    include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/tokenizers.cmake)
endif()

FUNCTION(relative_to_absolute_paths output)
    SET(listVar "")
    FOREACH(rel_path ${ARGN})
        get_filename_component(abs_path "${rel_path}" ABSOLUTE)
        LIST(APPEND listVar ${abs_path})
    ENDFOREACH(rel_path)
    SET(${output} "${listVar}" PARENT_SCOPE)
ENDFUNCTION(relative_to_absolute_paths)

set(HAILORT_CPP_SOURCES
    hailort.cpp
    hailort_defaults.cpp
)

add_subdirectory(utils)
add_subdirectory(os)
add_subdirectory(device_common)
add_subdirectory(device)
add_subdirectory(vdevice)
add_subdirectory(transform)
add_subdirectory(stream_common)
add_subdirectory(vdma)
add_subdirectory(hef)
add_subdirectory(network_group)
add_subdirectory(core_op)
add_subdirectory(net_flow)
add_subdirectory(rpc_callbacks)
add_subdirectory(genai)
add_subdirectory(perfetto)

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
relative_to_absolute_paths(DRIVER_OS_DIR ${DRIVER_OS_DIR})
relative_to_absolute_paths(HAILO_DRIVER_SRC_FILES ${HAILO_DRIVER_SRC_FILES})
set(HAILO_OS_DIR ${HAILO_OS_DIR} CACHE INTERNAL "Absolute path of os-dir")
set(HAILO_FULL_OS_DIR ${HAILO_FULL_OS_DIR} CACHE INTERNAL "Absolute Full path of os-dir")
set(DRIVER_OS_DIR ${DRIVER_OS_DIR} CACHE INTERNAL "Absolute Full path of driver os-dir")
set(HAILO_DRIVER_SRC_FILES ${HAILO_DRIVER_SRC_FILES} CACHE INTERNAL "Absolute Full path of driver src files")
set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} CACHE INTERNAL "Absolute paths of hailort's cpp source files")
set(COMMON_C_SOURCES ${COMMON_C_SOURCES} CACHE INTERNAL "Absolute paths of common source files")
set(HAILORT_SRCS_ABS ${HAILORT_CPP_SOURCES} ${COMMON_C_SOURCES} ${HRPC_CPP_SOURCES} ${HRPC_PROTOCOL_CPP_SOURCES} ${GENAI_SCHEME_CPP_SOURCES} CACHE INTERNAL "All absolute paths of hailort's source files")

SET_SOURCE_FILES_PROPERTIES(${C_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(libhailort SHARED ${HAILORT_SRCS_ABS})

get_property(PERFETTO_DEFS GLOBAL PROPERTY PERFETTO_COMPILE_DEFS)
target_compile_definitions(libhailort PRIVATE ${PERFETTO_DEFS})

# Include libraries
if(WIN32)
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
target_link_libraries(libhailort PRIVATE profiler_proto)
target_link_libraries(libhailort PRIVATE scheduler_mon_proto)
target_link_libraries(libhailort PRIVATE hailort_common)
target_link_libraries(libhailort PRIVATE Eigen3::Eigen)
target_link_libraries(libhailort PRIVATE rpc_proto)
target_link_libraries(libhailort PRIVATE genai_scheme_proto)
target_link_libraries(libhailort PRIVATE minja_wrapper)

# Link tokenizers when client tokenizer is enabled
if(HAILO_BUILD_CLIENT_TOKENIZER)
    target_link_libraries(libhailort PRIVATE tokenizers_cpp)
endif()

if(ENABLE_PERFETTO)
    target_link_libraries(libhailort PRIVATE perfetto)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL QNX)
    target_link_libraries(libhailort PRIVATE pci)
endif()

set(HAILORT_PUBLIC_HEADERS
    ${HAILORT_INC_DIR}/hailo/hailort.h
    ${HAILORT_INC_DIR}/hailo/platform.h

    ${HAILORT_INC_DIR}/hailo/hailort.hpp
    ${HAILORT_INC_DIR}/hailo/buffer.hpp
    ${HAILORT_INC_DIR}/hailo/device.hpp
    ${HAILORT_INC_DIR}/hailo/event.hpp
    ${HAILORT_INC_DIR}/hailo/expected.hpp
    ${HAILORT_INC_DIR}/hailo/hailort_common.hpp
    ${HAILORT_INC_DIR}/hailo/hef.hpp
    ${HAILORT_INC_DIR}/hailo/network_group.hpp
    ${HAILORT_INC_DIR}/hailo/stream.hpp
    ${HAILORT_INC_DIR}/hailo/transform.hpp
    ${HAILORT_INC_DIR}/hailo/vstream.hpp
    ${HAILORT_INC_DIR}/hailo/inference_pipeline.hpp
    ${HAILORT_INC_DIR}/hailo/infer_model.hpp
    ${HAILORT_INC_DIR}/hailo/runtime_statistics.hpp
    ${HAILORT_INC_DIR}/hailo/vdevice.hpp
    ${HAILORT_INC_DIR}/hailo/quantization.hpp
    ${HAILORT_INC_DIR}/hailo/hailort_defaults.hpp
    ${HAILORT_INC_DIR}/hailo/dma_mapped_buffer.hpp
    ${HAILORT_INC_DIR}/hailo/hailo_session.hpp
    ${HAILORT_INC_DIR}/hailo/hailo_gst_tensor_metadata.hpp

    # GenAI
    ${HAILORT_INC_DIR}/hailo/genai/common.hpp
    ${HAILORT_INC_DIR}/hailo/genai/llm/llm.hpp
    ${HAILORT_INC_DIR}/hailo/genai/vlm/vlm.hpp
    ${HAILORT_INC_DIR}/hailo/genai/speech2text/speech2text.hpp
)

set_target_properties(libhailort PROPERTIES
    PUBLIC_HEADER "${HAILORT_PUBLIC_HEADERS}"
    PREFIX ""
    VERSION ${HAILORT_MAJOR_VERSION}.${HAILORT_MINOR_VERSION}.${HAILORT_REVISION_VERSION}
    # SOVERSION ${HAILORT_MAJOR_VERSION}

    CXX_STANDARD              17
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
    $<BUILD_INTERFACE:${RPC_DIR}>
    $<BUILD_INTERFACE:${HRPC_DIR}>
    $<BUILD_INTERFACE:${HAILORT_GENAI_DIR}>
)

# Add tokenizer include directory when enabled
if(HAILO_BUILD_CLIENT_TOKENIZER)
    target_include_directories(libhailort PRIVATE
        $<BUILD_INTERFACE:${HAILORT_TOKENIZER_DIR}>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/hailort/hailort_server/genai>
    )
    target_compile_definitions(libhailort PRIVATE HAILO_CLIENT_TOKENIZER_ENABLED)
endif()

target_compile_definitions(libhailort PUBLIC
    -DHAILORT_MAJOR_VERSION=${HAILORT_MAJOR_VERSION}
    -DHAILORT_MINOR_VERSION=${HAILORT_MINOR_VERSION}
    -DHAILORT_REVISION_VERSION=${HAILORT_REVISION_VERSION}
)

# TODO: HRT-15676
install(DIRECTORY ${HAILORT_INC_DIR}/hailo/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/hailo")
install(TARGETS libhailort
    EXPORT HailoRTTargets
    CONFIGURATIONS Release
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    # PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/hailo"
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
  COMPATIBILITY SameMajorVersion
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
