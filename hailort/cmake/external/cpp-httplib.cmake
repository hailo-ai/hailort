cmake_minimum_required(VERSION 3.11.0)

include(FetchContent)

FetchContent_Declare(
    cpp-httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG 51dee793fec2fa70239f5cf190e165b54803880f # v0.18.2
    GIT_SHALLOW TRUE
    SOURCE_DIR ${HAILO_EXTERNAL_DIR}/cpp-httplib-src
    SUBBUILD_DIR ${HAILO_EXTERNAL_DIR}/cpp-httplib-subbuild
)

# cpp-httplib auto-detects optional deps (Brotli/ZLIB/OpenSSL) via find_package and links whatever the
# host provides. We don't use them, and during cross-compile they resolve to host libs and break linking
# ("file in wrong format"). Disable them for deterministic, host-independent builds.
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)

# https://stackoverflow.com/questions/65527126/disable-install-for-fetchcontent
FetchContent_GetProperties(cpp-httplib)
if(NOT cpp-httplib_POPULATED)
    FetchContent_Populate(cpp-httplib)
    if (NOT HAILO_EXTERNALS_EXCLUDE_TARGETS)
        add_subdirectory(${cpp-httplib_SOURCE_DIR} ${cpp-httplib_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
endif()