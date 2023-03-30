cmake_minimum_required(VERSION 3.15.0)

pybind11_add_module(_pyhailort_internal SHARED
    pyhailort_internal.cpp
    control_api.cpp
    $<TARGET_OBJECTS:libhailort>
)

add_dependencies(_pyhailort_internal libhailort)

set_target_properties(_pyhailort_internal PROPERTIES
    CXX_STANDARD              14
    CXX_STANDARD_REQUIRED     YES
)

target_include_directories(_pyhailort_internal
    PRIVATE
    $<BUILD_INTERFACE:${PYHAILORT_DIR}>
    $<BUILD_INTERFACE:${HAILORT_INC_DIR}>
    $<BUILD_INTERFACE:${HAILORT_COMMON_DIR}>
    $<BUILD_INTERFACE:${HAILORT_SRC_DIR}>
    $<BUILD_INTERFACE:${COMMON_INC_DIR}>
    $<BUILD_INTERFACE:${DRIVER_INC_DIR}>
)

target_link_libraries(_pyhailort_internal PRIVATE
    hef_proto
    spdlog::spdlog
    readerwriterqueue
    scheduler_mon_proto)
if(HAILO_BUILD_SERVICE)
    target_link_libraries(_pyhailort_internal PRIVATE grpc++_unsecure hailort_rpc_grpc_proto)
endif()

if(WIN32)
    target_link_libraries(_pyhailort_internal PRIVATE Ws2_32 Iphlpapi Shlwapi)
endif()

target_compile_options(_pyhailort_internal PRIVATE ${HAILORT_COMPILE_OPTIONS})
exclude_archive_libs_symbols(_pyhailort_internal)
