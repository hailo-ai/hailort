cmake_minimum_required(VERSION 3.5.0)

project(gsthailo)

include(GNUInstallDirs)

find_package(HailoRT 4.23.0 EXACT REQUIRED)

# GST_PLUGIN_DEFINE needs PACKAGE to be defined
set(GST_HAILO_PACKAGE_NAME "hailo")
set(GST_HAILO_VERSION "1.0")

include(find_libs_for_gstreamer.cmake)

set(GSTHAILO_SOURCES
    gst-hailo/gsthailoplugin.cpp
    gst-hailo/sync_gsthailonet.cpp
    gst-hailo/sync_gst_hailosend.cpp
    gst-hailo/sync_gst_hailorecv.cpp
    gst-hailo/gsthailonet.cpp
    gst-hailo/gsthailo_allocator.cpp
    gst-hailo/gsthailodevicestats.cpp
    gst-hailo/common.cpp
    gst-hailo/network_group_handle.cpp
    gst-hailo/metadata/hailo_buffer_flag_meta.cpp
    gst-hailo/metadata/tensor_meta.cpp
    gst-hailo/hailo_events/hailo_events.cpp)

# dmabuf is supported only on linux
if (UNIX)
    list(APPEND GSTHAILO_SOURCES gst-hailo/gsthailo_dmabuf_allocator.cpp gst-hailo/os/linux/dma_buf_allocator_wrapper.cpp)
elseif (MSVC)
    list(APPEND GSTHAILO_SOURCES gst-hailo/os/windows/dma_buf_allocator_wrapper.cpp)
endif ()

add_library(gsthailo SHARED ${GSTHAILO_SOURCES})

set_property(TARGET gsthailo PROPERTY CXX_STANDARD 14)

set_target_properties(gsthailo PROPERTIES
    CXX_STANDARD              14
    CXX_STANDARD_REQUIRED     YES
    CXX_EXTENSIONS            NO
    C_VISIBILITY_PRESET       hidden
    CXX_VISIBILITY_PRESET     hidden
    # VISIBILITY_INLINES_HIDDEN YES
)

if (UNIX)
    set(HAILORT_COMPILE_OPTIONS
        ${HAILORT_COMPILE_OPTIONS}
        -DVERSION="${GST_HAILO_VERSION}"
        -DPACKAGE="${GST_HAILO_PACKAGE_NAME}"
    )
elseif (WIN32)
    set(HAILORT_COMPILE_OPTIONS
        ${HAILORT_COMPILE_OPTIONS}
        /DWIN32_LEAN_AND_MEAN
        /DNOMINMAX                  # NOMINMAX is required in order to play nice with std::min/std::max (otherwise Windows.h defines it's own)
        /D_HAILO_EXPORTING
        /wd4201                     # Anonymous union/struct
        /wd4251                     # C++ ABI with STL
        -DVERSION="${GST_HAILO_VERSION}"
        -DPACKAGE="${GST_HAILO_PACKAGE_NAME}"
    )
else()
    message(FATAL_ERROR "Unexpeced host, stopping build")
endif()

target_compile_options(gsthailo PRIVATE ${HAILORT_COMPILE_OPTIONS})

set(GST_METADATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/gst-hailo/metadata/")

if (UNIX)
    target_include_directories(gsthailo PRIVATE ${GSTREAMER_VIDEO_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/gst-hailo ${CMAKE_CURRENT_SOURCE_DIR}/gst-hailo/os/linux ${GST_METADATA_DIR})
    target_link_libraries(gsthailo HailoRT::libhailort ${GSTREAMER_VIDEO_LDFLAGS} -lgstallocators-1.0)
else()
    target_include_directories(gsthailo PRIVATE ${GSTREAMER_INCLUDE_DIRS} ${GSTREAMER_VIDEO_INCLUDE_DIRS} ${GLIB_INCLUDE_DIRS} ${GLIBCONFIG_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/gst-hailo ${CMAKE_CURRENT_SOURCE_DIR}/gst-hailo/os/windows ${GST_METADATA_DIR})
    target_link_libraries(gsthailo HailoRT::libhailort ${GSTREAMER_LIBRARIES} ${GSTREAMER_BASE_LIBRARIES} ${GSTREAMER_VIDEO_LIBRARIES} ${GLIB_LIBRARIES} ${GOBJECT_LIBRARIES} -lgstallocators-1.0)
endif()

if (UNIX)
    set(GSTREAMER_LIB_DEST "${CMAKE_INSTALL_LIBDIR}/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu/gstreamer-1.0/")
    set(PUBLIC_HEADER_DEST "${CMAKE_INSTALL_INCLUDEDIR}/gstreamer-1.0/gst/hailo")
elseif (MSVC)
    if ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "AMD64" OR "${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x64")
        set(GSTREAMER_DEST "C:\\gstreamer\\1.0\\msvc_x86_64")
    else()
        message(FATAL_ERROR "HailoRT GStreamer elements installation is not supported on this architecture.")
    endif()
    set(GSTREAMER_LIB_DEST "${GSTREAMER_DEST}\\lib\\gstreamer-1.0")
    set(PUBLIC_HEADER_DEST "${CMAKE_INSTALL_INCLUDEDIR}\\gstreamer-1.0\\gst\\hailo")
endif()

# Install command using the set variables
install(TARGETS gsthailo
    RUNTIME DESTINATION "${GSTREAMER_LIB_DEST}"
    LIBRARY DESTINATION "${GSTREAMER_LIB_DEST}"
    ARCHIVE DESTINATION "${GSTREAMER_LIB_DEST}"
    CONFIGURATIONS Release)

# Install the entire gst-hailo/include directory and its contents
install(DIRECTORY gst-hailo/include
    DESTINATION ${PUBLIC_HEADER_DEST}
)

install(FILES
    gst-hailo/metadata/tensor_meta.hpp
    DESTINATION ${PUBLIC_HEADER_DEST}
)
