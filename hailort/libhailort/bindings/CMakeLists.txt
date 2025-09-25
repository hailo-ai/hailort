cmake_minimum_required(VERSION 3.5.0)

# QNX currently doesnt support GStreamer
if(HAILO_BUILD_GSTREAMER AND NOT CMAKE_SYSTEM_NAME STREQUAL QNX)
    add_subdirectory(gstreamer)
endif()

option(HAILO_BUILD_PYHAILORT_INTERNAL OFF)
option(HAILO_BUILD_RAW_CONNECTION OFF)
if(HAILO_BUILD_RAW_CONNECTION OR HAILO_BUILD_PYHAILORT_INTERNAL)
    add_subdirectory(python/src/internal/)
endif()
