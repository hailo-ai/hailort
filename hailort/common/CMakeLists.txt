cmake_minimum_required(VERSION 3.5.0)

include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/spdlog.cmake)
include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/readerwriterqueue.cmake)
include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/xxhash.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/common_compiler_options.cmake)


set(HAILORT_COMMON_CPP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/barrier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/file_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/event_internal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/fork_support.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/buffer_pool.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/device_measurements.cpp
)

if(WIN32)
    add_subdirectory(os/windows)
elseif(UNIX)
    add_subdirectory(os/posix)
else()
    message(FATAL_ERROR "Unexpeced host, stopping build")
endif()

# Create static library hailort_common
add_library(hailort_common STATIC ${HAILORT_COMMON_CPP_SOURCES})
target_include_directories(hailort_common
    PUBLIC
    $<BUILD_INTERFACE:${HAILORT_INC_DIR}>
    $<BUILD_INTERFACE:${HAILORT_COMMON_DIR}>
    $<BUILD_INTERFACE:${COMMON_INC_DIR}>
)

target_link_libraries(hailort_common
    PUBLIC
    spdlog::spdlog
    readerwriterqueue
    xxhash
)

if(CMAKE_SYSTEM_NAME STREQUAL QNX)
    include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/pevents.cmake)
    target_link_libraries(hailort_common PRIVATE pevents pci)
endif()

set_target_properties(hailort_common PROPERTIES
    CXX_STANDARD              14
    CXX_STANDARD_REQUIRED     YES
    CXX_EXTENSIONS            NO
    POSITION_INDEPENDENT_CODE ON
)

target_compile_options(hailort_common PRIVATE ${HAILORT_COMPILE_OPTIONS})
disable_exceptions(hailort_common)
exclude_archive_libs_symbols(hailort_common)


if(WIN32)
    target_link_libraries(hailort_common PRIVATE
        Ws2_32                  # Winsock2 API
        Iphlpapi                # IP Helper API (i.e GetIpNetTable)
        Shlwapi                 # Shell API (i.e PathIsDirectoryA)
    )
elseif(NOT CMAKE_SYSTEM_NAME STREQUAL Android)
    # TODO: HRT-14770 fix android build
    target_link_libraries(hailort_common PRIVATE
        rt                       # Used for shm api (i.e shm_open)
    )
endif()
