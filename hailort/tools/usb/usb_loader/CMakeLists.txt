# cmake --install, used by the documented standalone installation flow, was added in 3.15.
cmake_minimum_required(VERSION 3.15)

# The number of levels tracks this file's depth: hailort/tools/usb/usb_loader.
get_filename_component(HAILO_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)

# Built either standalone from a hailort clone, or as part of the HailoRT tree.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(HAILO_USB_LOADER_IS_STANDALONE ON)
    project(hailo_usb_loader C)
    set(HAILO_EXTERNALS_CMAKE_SCRIPTS ${HAILO_REPOSITORY_ROOT}/hailort/cmake/external)
    set(HAILO_EXTERNAL_DIR ${HAILO_REPOSITORY_ROOT}/hailort/external)
endif()

include(GNUInstallDirs)

if(HAILO_USB_LOADER_IS_STANDALONE)
    set(HAILO_EXTERNALS_EXCLUDE_TARGETS ON) # populate libusb sources only, targets are created below
endif()
include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/libusb.cmake)

if(HAILO_USB_LOADER_IS_STANDALONE AND NOT TARGET LibUSB::LibUSB)
    # No system libusb was found, which also means no libusb development package: the host has
    # neither the unversioned libusb-1.0.so a shared build links against, nor libudev. Build
    # libusb static and udev-free so the installed loader resolves nothing but libc.
    set(LIBUSB_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(LIBUSB_ENABLE_UDEV OFF CACHE BOOL "" FORCE)
    add_subdirectory(${libusb-cmake_SOURCE_DIR} ${libusb-cmake_BINARY_DIR} EXCLUDE_FROM_ALL)
    add_library(LibUSB::LibUSB INTERFACE IMPORTED GLOBAL)
    target_link_libraries(LibUSB::LibUSB INTERFACE usb-1.0)
endif()

include(${HAILO_REPOSITORY_ROOT}/cmake/usb_fw_version.cmake)

add_executable(hailo_usb_loader hailo_usb_loader.c)
set_property(TARGET hailo_usb_loader PROPERTY C_STANDARD 99)
target_compile_options(hailo_usb_loader PRIVATE ${HAILORT_COMPILE_OPTIONS})
target_include_directories(hailo_usb_loader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/generated/loader_protocol
)
target_link_libraries(hailo_usb_loader PRIVATE LibUSB::LibUSB)
install(TARGETS hailo_usb_loader RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Copy the DLL next to hailo_usb_loader.exe so runtime resolution and downstream
# packaging pick it up without needing a pre-installed libusb on the host.
if(WIN32 AND TARGET usb-1.0)
    get_target_property(LIBUSB_TARGET_TYPE usb-1.0 TYPE)
    if(LIBUSB_TARGET_TYPE STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET hailo_usb_loader POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:usb-1.0> $<TARGET_FILE_DIR:hailo_usb_loader>
            COMMENT "Copying libusb DLL next to hailo_usb_loader"
        )
    endif()
endif()
