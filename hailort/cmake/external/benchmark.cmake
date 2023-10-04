cmake_minimum_required(VERSION 3.11.0)

include(FetchContent)

# BENCHMARK_ENABLE_TESTING can be used by other 3rd party projects, therefore we define it
# before adding projects
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Enable testing of the benchmark library.")

FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git 
    GIT_TAG f91b6b42b1b9854772a90ae9501464a161707d1e # Version 1.6.0
    GIT_SHALLOW TRUE
    SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/benchmark"
    BINARY_DIR "${CMAKE_CURRENT_LIST_DIR}/benchmark"
)

if(NOT HAILO_OFFLINE_COMPILATION)
    # https://stackoverflow.com/questions/65527126/disable-install-for-fetchcontent
    FetchContent_GetProperties(benchmark)
    if(NOT benchmark_POPULATED)
        FetchContent_Populate(benchmark)
        add_subdirectory(${benchmark_SOURCE_DIR} ${benchmark_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
else()
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/benchmark EXCLUDE_FROM_ALL)
endif()