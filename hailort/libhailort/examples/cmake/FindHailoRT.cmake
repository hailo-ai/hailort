# - Try to find HailoRT
#   - If libhailort is defined (building as part of the build tree), use it
#   - Otherwise, find HAILORT_LIB and HAILORT_INCLUDE_DIR, and import libhailort

if (NOT TARGET libhailort)
    # find_path finds a directory containing the named file
    if(WIN32)
        find_library(HAILORT_LIB "libhailort.lib" PATH_SUFFIXES "HailoRT/lib/")
        find_path(HAILORT_INCLUDE_DIR "hailo/" PATH_SUFFIXES "HailoRT/include/")

    else()
        find_library(HAILORT_LIB "libhailort.so.4.6.0" PATH_SUFFIXES "lib/")
        find_path(HAILORT_INCLUDE_DIR "hailo/" PATH_SUFFIXES "include/")
    endif()

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
        IMPORTED_IMPLIB "${HAILORT_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${HAILORT_INCLUDE_DIR}"
    )

else()
    add_library(HailoRT::libhailort ALIAS libhailort)
endif()
