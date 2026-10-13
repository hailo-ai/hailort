cmake_minimum_required(VERSION 3.5.0)

if(HAILO_BUILD_GSTREAMER)
    add_subdirectory(gstreamer)
endif()

option(HAILO_BUILD_PYHAILORT_INTERNAL OFF)
if(HAILO_BUILD_PYHAILORT_INTERNAL)
    add_subdirectory(python/src/internal/)
endif()
