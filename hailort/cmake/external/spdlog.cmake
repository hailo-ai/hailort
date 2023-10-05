cmake_minimum_required(VERSION 3.11.0)

include(FetchContent)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog
    GIT_TAG 22a169bc319ac06948e7ee0be6b9b0ac81386604
    GIT_SHALLOW TRUE
    SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/spdlog"
    BINARY_DIR "${CMAKE_CURRENT_LIST_DIR}/spdlog"
)

if(NOT HAILO_OFFLINE_COMPILATION)
    # https://stackoverflow.com/questions/65527126/disable-install-for-fetchcontent
    FetchContent_GetProperties(spdlog)
    if(NOT spdlog_POPULATED)
        FetchContent_Populate(spdlog)
        add_subdirectory(${spdlog_SOURCE_DIR} ${spdlog_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
else()
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/spdlog EXCLUDE_FROM_ALL)
endif()
set_target_properties(spdlog PROPERTIES POSITION_INDEPENDENT_CODE ON)
