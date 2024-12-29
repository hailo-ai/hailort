cmake_minimum_required(VERSION 3.11.0)

include(FetchContent)

FetchContent_Declare(
    xxhash
    GIT_REPOSITORY https://github.com/Cyan4973/xxHash
    GIT_TAG bbb27a5efb85b92a0486cf361a8635715a53f6ba # Version 0.8.2
    GIT_SHALLOW TRUE
    SOURCE_DIR ${HAILO_EXTERNAL_DIR}/xxhash-src
    SUBBUILD_DIR ${HAILO_EXTERNAL_DIR}/xxhash-subbuild
)

# https://stackoverflow.com/questions/65527126/disable-install-for-fetchcontent
FetchContent_GetProperties(xxhash)
if(NOT xxhash_POPULATED)
    FetchContent_Populate(xxhash)
    if (NOT HAILO_EXTERNALS_EXCLUDE_TARGETS)
        # Add xxhash as a header-only library
        add_library(xxhash INTERFACE)
        target_include_directories(xxhash INTERFACE ${xxhash_SOURCE_DIR})
    endif()
endif()