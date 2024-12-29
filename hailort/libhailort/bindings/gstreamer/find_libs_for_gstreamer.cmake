cmake_minimum_required(VERSION 3.5.0)

if (UNIX)
    include(find_libs_for_gstreamer_linux.cmake)
elseif (MSVC)
    include(find_libs_for_gstreamer_windows.cmake)
else()
    message(FATAL_ERROR "HailoRT GStreamer elements compilation is supported only on UNIX or MSVC.")
endif()