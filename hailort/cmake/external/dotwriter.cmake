cmake_minimum_required(VERSION 3.11.0)

include(FetchContent)

FetchContent_Declare(
    dotwriter
    GIT_REPOSITORY https://github.com/hailo-ai/DotWriter
    GIT_TAG e5fa8f281adca10dd342b1d32e981499b8681daf # Version master
    GIT_SHALLOW TRUE
    SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/dotwriter"
    BINARY_DIR "${CMAKE_CURRENT_LIST_DIR}/dotwriter"
)

if(NOT HAILO_OFFLINE_COMPILATION)
    # https://stackoverflow.com/questions/65527126/disable-install-for-fetchcontent
    FetchContent_GetProperties(dotwriter)
    if(NOT dotwriter_POPULATED)
        FetchContent_Populate(dotwriter)
        add_subdirectory(${dotwriter_SOURCE_DIR} ${dotwriter_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
else()
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/dotwriter EXCLUDE_FROM_ALL)
endif()