cmake_minimum_required(VERSION 3.11.0)

include(FetchContent)

FetchContent_Declare(
    libnpy
    GIT_REPOSITORY https://github.com/llohse/libnpy.git
    GIT_TAG 890ea4fcda302a580e633c624c6a63e2a5d422f6 # v1.0.1
    GIT_SHALLOW TRUE
    SOURCE_DIR ${HAILO_EXTERNAL_DIR}/libnpy-src
    SUBBUILD_DIR ${HAILO_EXTERNAL_DIR}/libnpy-subbuild
)

# https://stackoverflow.com/questions/65527126/disable-install-for-fetchcontent
FetchContent_GetProperties(libnpy)
if(NOT libnpy_POPULATED)
    FetchContent_Populate(libnpy)
    if (NOT HAILO_EXTERNALS_EXCLUDE_TARGETS)
        # Add libnpy as a header-only library
        add_library(libnpy INTERFACE)
        target_include_directories(libnpy INTERFACE ${libnpy_SOURCE_DIR}/include)
    endif()
endif()