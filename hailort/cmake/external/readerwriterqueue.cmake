cmake_minimum_required(VERSION 3.11.0)

include(FetchContent)

FetchContent_Declare(
    readerwriterqueue
    GIT_REPOSITORY https://github.com/cameron314/readerwriterqueue
    GIT_TAG 435e36540e306cac40fcfeab8cc0a22d48464509 # Version 1.0.3
    GIT_SHALLOW TRUE
    SOURCE_DIR ${HAILO_EXTERNAL_DIR}/readerwriterqueue-src
    SUBBUILD_DIR ${HAILO_EXTERNAL_DIR}/readerwriterqueue-subbuild
)

# https://stackoverflow.com/questions/65527126/disable-install-for-fetchcontent
FetchContent_GetProperties(readerwriterqueue)
if(NOT readerwriterqueue_POPULATED)
    FetchContent_Populate(readerwriterqueue)
    if (NOT HAILO_EXTERNALS_EXCLUDE_TARGETS)
        # Add readerwriterqueue as a header-only library
        add_library(readerwriterqueue INTERFACE)
        target_include_directories(readerwriterqueue INTERFACE ${readerwriterqueue_SOURCE_DIR})
    endif()
endif()