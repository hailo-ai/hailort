cmake_minimum_required(VERSION 3.11.0)

include(FetchContent)

FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git 
    GIT_TAG c4e3767e265808590986d5db6ca1b5532a7f3d13 # Version 2.13.7
    GIT_SHALLOW TRUE
    SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/catch2"
    BINARY_DIR "${CMAKE_CURRENT_LIST_DIR}/catch2"
)

if(NOT HAILO_OFFLINE_COMPILATION)
    # https://stackoverflow.com/questions/65527126/disable-install-for-fetchcontent
    FetchContent_GetProperties(catch2)
    if(NOT catch2_POPULATED)
        FetchContent_Populate(catch2)
        add_subdirectory(${catch2_SOURCE_DIR} ${catch2_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
else()
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/catch2 EXCLUDE_FROM_ALL)
endif()