cmake_minimum_required(VERSION 3.0.0)

pybind11_add_module(_pyhailort_internal SHARED
    pyhailort_internal.cpp
    control_api.cpp
    ${HAILORT_SRCS_ABS}
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

target_link_libraries(_pyhailort_internal PRIVATE libhailort hef_proto spdlog::spdlog readerwriterqueue)
if(WIN32)
    target_link_libraries(_pyhailort_internal PRIVATE Ws2_32 Iphlpapi Shlwapi)
endif()

target_compile_options(_pyhailort_internal PRIVATE ${HAILORT_COMPILE_OPTIONS})
exclude_archive_libs_symbols(_pyhailort_internal)