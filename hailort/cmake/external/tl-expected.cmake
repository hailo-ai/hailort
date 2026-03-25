cmake_minimum_required(VERSION 3.11.0)

include(FetchContent)

FetchContent_Declare(
    tl-expected
    GIT_REPOSITORY https://github.com/TartanLlama/expected.git
    GIT_TAG 1770e3559f2f6ea4a5fb4f577ad22aeb30fbd8e4 # Version 1.3.1
    GIT_SHALLOW TRUE
    SOURCE_DIR ${HAILO_EXTERNAL_DIR}/tl-expected-src
    SUBBUILD_DIR ${HAILO_EXTERNAL_DIR}/tl-expected-subbuild
)

# https://stackoverflow.com/questions/65527126/disable-install-for-fetchcontent
FetchContent_GetProperties(tl-expected)
if(NOT tl-expected_POPULATED)
    FetchContent_Populate(tl-expected)
    option(EXPECTED_BUILD_TESTS OFF)
    if (NOT HAILO_EXTERNALS_EXCLUDE_TARGETS)
        add_subdirectory(${tl-expected_SOURCE_DIR} ${tl-expected_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
endif()
