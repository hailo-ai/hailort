cmake_minimum_required(VERSION 3.0.0)

project(gsthailo)

include(GNUInstallDirs)

if(NOT CMAKE_HOST_UNIX)
    message(FATAL_ERROR "Only unix hosts are supported, stopping build")
endif()

find_package(HailoRT 4.17.1 EXACT REQUIRED)

# GST_PLUGIN_DEFINE needs PACKAGE to be defined
set(GST_HAILO_PACKAGE_NAME "hailo")
set(GST_HAILO_VERSION "1.0")

find_package(PkgConfig REQUIRED)
pkg_search_module(GLIB REQUIRED glib-2.0)
pkg_search_module(GSTREAMER REQUIRED gstreamer-1.0)
pkg_search_module(GSTREAMER_BASE REQUIRED gstreamer-base-1.0)
pkg_search_module(GSTREAMER_VIDEO REQUIRED gstreamer-video-1.0)
pkg_search_module(GSTREAMER_PLUGINS_BASE REQUIRED gstreamer-plugins-base-1.0)

add_library(gsthailo SHARED
    gst-hailo/gsthailoplugin.cpp
    gst-hailo/sync_gsthailonet.cpp
    gst-hailo/sync_gst_hailosend.cpp
    gst-hailo/sync_gst_hailorecv.cpp
    gst-hailo/gsthailonet.cpp
    gst-hailo/gsthailodevicestats.cpp
    gst-hailo/common.cpp
    gst-hailo/network_group_handle.cpp
    gst-hailo/metadata/hailo_buffer_flag_meta.cpp
    gst-hailo/metadata/tensor_meta.cpp
    gst-hailo/hailo_events/hailo_events.cpp)

set_property(TARGET gsthailo PROPERTY CXX_STANDARD 14)

set_target_properties(gsthailo PROPERTIES
    PUBLIC_HEADER "gst-hailo/metadata/tensor_meta.hpp"
    CXX_STANDARD              14
    CXX_STANDARD_REQUIRED     YES
    CXX_EXTENSIONS            NO
    C_VISIBILITY_PRESET       hidden
    CXX_VISIBILITY_PRESET     hidden
    # VISIBILITY_INLINES_HIDDEN YES
)

target_compile_options(gsthailo PRIVATE
    -Werror -Wall -Wextra -Wconversion
    -DVERSION="${GST_HAILO_VERSION}"
    -DPACKAGE="${GST_HAILO_PACKAGE_NAME}")

target_include_directories(gsthailo PRIVATE ${GSTREAMER_VIDEO_INCLUDE_DIRS})
target_link_libraries(gsthailo HailoRT::libhailort ${GSTREAMER_VIDEO_LDFLAGS} -lgstallocators-1.0)

install(TARGETS gsthailo
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    # TODO: get gstreamer-1.0 in an automate way
    PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/gstreamer-1.0/gst/hailo/"
    CONFIGURATIONS Release)
