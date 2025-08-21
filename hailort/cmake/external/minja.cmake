cmake_minimum_required(VERSION 3.11.0)

include(FetchContent)

FetchContent_Declare(
    minja
    GIT_REPOSITORY https://github.com/google/minja
    GIT_TAG 58568621432715b0ed38efd16238b0e7ff36c3ba # main
    # GIT_SHALLOW TRUE
    SOURCE_DIR ${HAILO_EXTERNAL_DIR}/minja-src
    SUBBUILD_DIR ${HAILO_EXTERNAL_DIR}/minja-subbuild
)

# https://stackoverflow.com/questions/65527126/disable-install-for-fetchcontent
FetchContent_GetProperties(minja)
if(NOT minja_POPULATED)
    FetchContent_Populate(minja)
    if (NOT HAILO_EXTERNALS_EXCLUDE_TARGETS)
        # Add minja as a header-only library
        add_library(minja INTERFACE)
        target_include_directories(minja INTERFACE ${minja_SOURCE_DIR}/include)
    endif()
endif()