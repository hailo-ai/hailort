cmake_minimum_required(VERSION 3.5.0)

find_program(CCACHE_PROGRAM ccache)
find_program(CLACHE_PROGRAM clcache)

if(CCACHE_PROGRAM)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
elseif(CLCACHE_PROGRAM)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CLCACHE_PROGRAM}")
endif()

if(WIN32)
    find_program(SCCACHE sccache)
    if(SCCACHE)
        set(CMAKE_C_COMPILER_LAUNCHER ${SCCACHE})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${SCCACHE})
        set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
        cmake_policy(SET CMP0141 NEW)
    endif()
endif()

project(HailoRT)

# Prevent in-tree building
if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(FATAL_ERROR "In-source builds are not allowed.
    Please remove the `CMakeCache.txt` file and `CMakeFiles` directory from `${CMAKE_SOURCE_DIR}`
    In order to build, please create a new `build` directory and run `cmake ..` from there.")
endif()

# Set compiler flags in HAILORT_COMPILE_OPTIONS
# TODO: Change HAILORT_COMPILE_OPTIONS to add_compile_options
if(WIN32)
    # TODO: set this eventually? set(HAILORT_COMPILE_OPTIONS /Wall)
    set(HAILORT_COMPILE_OPTIONS ${HAILORT_COMPILE_OPTIONS}
        /W4
        /DWIN32_LEAN_AND_MEAN
        /DNOMINMAX                  # NOMINMAX is required in order to play nice with std::min/std::max (otherwise Windows.h defines it's own)
        /D_HAILO_EXPORTING
        /wd4201                     # Anonymous union/struct
        /wd4251                     # C++ ABI with STL
    )
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)  # Disable "unsafe function" warnings
elseif(UNIX)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "QCC")
        set(HAILORT_COMPILE_OPTIONS ${HAILORT_COMPILE_OPTIONS} -Wall -Wextra -Wconversion)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(HAILORT_COMPILE_OPTIONS ${HAILORT_COMPILE_OPTIONS} -Wall -Wextra
            # TODO: remove me warnings
            -Wno-conversion
            -Wno-deprecated-declarations  # On c structures with deprecated attribute, clang generates implicit move ctor
                                          # that causes a warning
            -Wno-inconsistent-missing-override
            )
    else()
        message(FATAL_ERROR "Invalid value for CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")
    endif()
else()
    message(FATAL_ERROR "Unexpeced host, stopping build")
endif()

# Enable output of compile commands during generation
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add subdirectories
add_subdirectory(hailort)
