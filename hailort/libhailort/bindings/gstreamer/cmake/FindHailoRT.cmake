# - Try to find HailoRT
#   - If libhailort is defined (building as part of the build tree), use it
#   - Otherwise, find HAILORT_LIB and HAILORT_INCLUDE_DIR, and import libhailort

if (NOT TARGET libhailort)
    # find_path finds a directory containing the named file
    find_path(HAILORT_INCLUDE_DIR "hailo/" PATH_SUFFIXES "include/")

    set(HAILORT_MAJOR_VERSION    4)
    set(HAILORT_MINOR_VERSION    7)
    set(HAILORT_REVISION_VERSION 0)

    find_library(HAILORT_LIB "libhailort.so.${HAILORT_MAJOR_VERSION}.${HAILORT_MINOR_VERSION}.${HAILORT_REVISION_VERSION}" PATH_SUFFIXES "lib/")

    include(FindPackageHandleStandardArgs)
    # Handle the QUIETLY and REQUIRED arguments and set HAILORT_FOUND to TRUE
    # if all listed variables are TRUE
    find_package_handle_standard_args(
        HailoRT
        DEFAULT_MSG
        HAILORT_LIB
        HAILORT_INCLUDE_DIR
    )

    add_library(HailoRT::libhailort SHARED IMPORTED)
    set_target_properties(HailoRT::libhailort PROPERTIES
        IMPORTED_LOCATION "${HAILORT_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${HAILORT_INCLUDE_DIR}"
    )

    set_property(TARGET HailoRT::libhailort
        APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
            HAILORT_MAJOR_VERSION=${HAILORT_MAJOR_VERSION}
            HAILORT_MINOR_VERSION=${HAILORT_MINOR_VERSION}
            HAILORT_REVISION_VERSION=${HAILORT_REVISION_VERSION}
    )

else()
    add_library(HailoRT::libhailort ALIAS libhailort)
endif()
