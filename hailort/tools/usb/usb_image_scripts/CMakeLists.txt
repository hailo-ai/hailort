cmake_minimum_required(VERSION 3.11.0)

set(HAILO_REPOSITORY_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../..)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(hailo_usb_loader C)
    set(HAILO_EXTERNALS_CMAKE_SCRIPTS ${HAILO_REPOSITORY_ROOT}/hailort/cmake/external/)
    set(HAILO_EXTERNAL_DIR ${HAILO_REPOSITORY_ROOT}/hailort/external)
    # Keep in sync with hailort/CMakeLists.txt, which supplies these in the in-tree build
    add_definitions( -DFIRMWARE_VERSION_MAJOR=5 )
    add_definitions( -DFIRMWARE_VERSION_MINOR=5 )
    add_definitions( -DFIRMWARE_VERSION_REVISION=0 )
endif()

include(GNUInstallDirs)
include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/libusb.cmake)
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

# libusb is built from source as a SHARED library on Windows (pinned in libusb.cmake).
# Copy the DLL next to hailo_usb_loader.exe so runtime resolution and downstream
# packaging pick it up without needing a pre-installed libusb on the host.
if(WIN32)
    add_custom_command(TARGET hailo_usb_loader POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:usb-1.0> $<TARGET_FILE_DIR:hailo_usb_loader>
        COMMENT "Copying libusb DLL next to hailo_usb_loader"
    )
endif()
