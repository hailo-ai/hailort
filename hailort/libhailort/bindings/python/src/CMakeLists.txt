cmake_minimum_required(VERSION 3.0.0)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    string(REPLACE "." "" dpython ${PYBIND11_PYTHON_VERSION}) # E.g "3.5" -> "35"
    if(${dpython} LESS "38")
        set(m_flag "m")
    else()
        set(m_flag "")
    endif()
    set(PYTHON_MODULE_EXTENSION ".cpython-${dpython}${m_flag}-${CMAKE_SYSTEM_PROCESSOR}-linux-gnu.so")
endif()

set(PYHAILORT_DIR ${CMAKE_CURRENT_LIST_DIR})
add_subdirectory(internal)
pybind11_add_module(_pyhailort
    pyhailort.cpp
    ${HAILORT_COMMON_CPP_SOURCES}
)
target_include_directories(_pyhailort
    PRIVATE
    $<BUILD_INTERFACE:${HAILORT_INC_DIR}>
    $<BUILD_INTERFACE:${HAILORT_COMMON_DIR}>
    $<BUILD_INTERFACE:${HAILORT_SRC_DIR}>
    $<BUILD_INTERFACE:${COMMON_INC_DIR}>
)

target_link_libraries(_pyhailort PRIVATE libhailort spdlog::spdlog)
if(WIN32)
    target_link_libraries(_pyhailort PRIVATE Ws2_32 Iphlpapi Shlwapi)
endif()
target_compile_options(_pyhailort PRIVATE ${HAILORT_COMPILE_OPTIONS})
exclude_archive_libs_symbols(_pyhailort)
