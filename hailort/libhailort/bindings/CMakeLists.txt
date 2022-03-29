cmake_minimum_required(VERSION 3.0.0)
if(HAILO_BUILD_PYBIND)
    add_subdirectory(python)
endif()
# QNX currently doesnt support GStreamer
if(HAILO_BUILD_GSTREAMER AND CMAKE_HOST_UNIX AND NOT CMAKE_SYSTEM_NAME STREQUAL QNX)
    add_subdirectory(gstreamer)
endif()
