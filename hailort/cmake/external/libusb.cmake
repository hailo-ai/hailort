cmake_minimum_required(VERSION 3.11.0)

# Guard against multiple inclusions — libusb.cmake is included from multiple CMakeLists.txt.
if(TARGET LibUSB::LibUSB)
    return()
endif()

# pkg-config (Linux, or Windows with vcpkg)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(_LIBUSB_PKG QUIET IMPORTED_TARGET libusb-1.0)
    if(_LIBUSB_PKG_FOUND)
        add_library(LibUSB::LibUSB ALIAS PkgConfig::_LIBUSB_PKG)
        message(STATUS "libusb found via pkg-config")
        return()
    endif()
endif()

include(FetchContent)

FetchContent_Declare(
    libusb-cmake
    GIT_REPOSITORY https://github.com/libusb/libusb-cmake.git
    GIT_TAG 92e156b075c0a33873d760a4b8ee37f06fdcb66d # version 1.0.29-0
    SOURCE_DIR ${HAILO_EXTERNAL_DIR}/libusb-cmake-src
    SUBBUILD_DIR ${HAILO_EXTERNAL_DIR}/libusb-cmake-subbuild
)

set(LIBUSB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(LIBUSB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LIBUSB_INSTALL_TARGETS OFF CACHE BOOL "" FORCE)
set(LIBUSB_BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)

FetchContent_GetProperties(libusb-cmake)
if(NOT libusb-cmake_POPULATED)
    FetchContent_Populate(libusb-cmake)
    if (NOT HAILO_EXTERNALS_EXCLUDE_TARGETS)
        add_subdirectory(${libusb-cmake_SOURCE_DIR} ${libusb-cmake_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
endif()

if (NOT HAILO_EXTERNALS_EXCLUDE_TARGETS)
    # Wrap usb-1.0 in an IMPORTED INTERFACE proxy instead of an ALIAS. install(EXPORT ...)
    # rejects an exported target whose link closure contains a non-imported real target
    # (`usb-1.0` here), but it stops walking when it hits an IMPORTED target. This lets
    # libhailort export HailoRTTargets cleanly while still linking the FetchContent-built
    # libusb at build time. On the pkg-config branch above, LibUSB::LibUSB is already an
    # ALIAS to an IMPORTED target, so the same property holds there.
    add_library(LibUSB::LibUSB INTERFACE IMPORTED GLOBAL)
    target_link_libraries(LibUSB::LibUSB INTERFACE usb-1.0)
endif()
