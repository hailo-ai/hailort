cmake_minimum_required(VERSION 3.0.0)

option(HAILO_BUILD_PYBIND "Build Python binding" OFF)
option(HAILO_BUILD_PYHAILORT_VENV "Build pyhailort in venv. Only used if HAILO_BUILD_PYBIND is on" ON)
option(HAILO_BUILD_EMULATOR "Build hailort for emulator" OFF)
option(HAILO_BUILD_UT "Build Unit Tests" OFF)
option(HAILO_BUILD_GSTREAMER "Compile gstreamer plugins" OFF)
option(HAILO_BUILD_EXAMPLES "Build examples" OFF)
option(HAILO_OFFLINE_COMPILATION "Don't download external dependencies" OFF)
option(HAILO_MICROPROFILE "Microprofile code" OFF)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

project(HailoRT)

# Prevent in-tree building
if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(FATAL_ERROR "In-source builds are not allowed.
    Please remove the `CMakeCache.txt` file and `CMakeFiles` directory from `${CMAKE_SOURCE_DIR}`
    In order to build, please create a new `build` directory and run `cmake ..` from there.")
endif()

# Check build type
if (NOT CMAKE_BUILD_TYPE)
    message(STATUS "No build type selected, default to Debug")
    set(CMAKE_BUILD_TYPE "Debug")
endif()
message(STATUS "Building ${PROJECT_NAME} in ${CMAKE_BUILD_TYPE}")

# Set compiler flags in HAILORT_COMPILE_OPTIONS
# TODO: Change HAILORT_COMPILE_OPTIONS to add_compile_options
if(WIN32)
    # TODO: set this eventually? set(HAILORT_COMPILE_OPTIONS /Wall)
    set(HAILORT_COMPILE_OPTIONS ${HAILORT_COMPILE_OPTIONS}
        /W4
        /WX
        /DWIN32_LEAN_AND_MEAN
        /DNOMINMAX                  # NOMINMAX is required in order to play nice with std::min/std::max (otherwise Windows.h defines it's own)
        /D_HAILO_EXPORTING
        /wd4201                     # Anonymous union/struct
        /wd4251                     # C++ ABI with STL
    )
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)  # Disable "unsafe function" warnings
elseif(UNIX)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "QCC")
        set(HAILORT_COMPILE_OPTIONS ${HAILORT_COMPILE_OPTIONS} -Werror -Wall -Wextra -Wconversion)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(HAILORT_COMPILE_OPTIONS ${HAILORT_COMPILE_OPTIONS} -Werror -Wall -Wextra
            # TODO: remove me warnings
            -Wno-conversion
            -Wno-deprecated-declarations
            -Wno-inconsistent-missing-override
            )
    else()
        message(FATAL_ERROR "Invalid value for CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")
    endif()
else()
    message(FATAL_ERROR "Unexpeced host, stopping build")
endif()

enable_testing()

# Flag for emulator (FPGA/Veloce)
if(HAILO_BUILD_EMULATOR)
    message(WARNING "HailoRT is building with Emulator flag on")
    set(HAILORT_COMPILE_OPTIONS ${HAILORT_COMPILE_OPTIONS} -DHAILO_EMULATOR)
endif()

# Enable output of compile commands during generation
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add subdirectories
add_subdirectory(hailort)
