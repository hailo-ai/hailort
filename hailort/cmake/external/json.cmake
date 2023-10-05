cmake_minimum_required(VERSION 3.11.0)

include(FetchContent)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/ArthurSonzogni/nlohmann_json_cmake_fetchcontent.git
    GIT_TAG 391786c6c3abdd3eeb993a3154f1f2a4cfe137a0 # Version 3.9.1
    GIT_SHALLOW TRUE
    SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/json"
    BINARY_DIR "${CMAKE_CURRENT_LIST_DIR}/json"
)

if(NOT HAILO_OFFLINE_COMPILATION)
    # https://stackoverflow.com/questions/65527126/disable-install-for-fetchcontent
    FetchContent_GetProperties(json)
    if(NOT json_POPULATED)
        FetchContent_Populate(json)
        add_subdirectory(${json_SOURCE_DIR} ${json_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
else()
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/json EXCLUDE_FROM_ALL)
endif()