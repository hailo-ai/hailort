#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "HailoRT::libhailort" for configuration "Release"
set_property(TARGET HailoRT::libhailort APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(HailoRT::libhailort PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libhailort.so.5.2.0"
  IMPORTED_SONAME_RELEASE "libhailort.so.5.2.0"
  )

list(APPEND _cmake_import_check_targets HailoRT::libhailort )
list(APPEND _cmake_import_check_files_for_HailoRT::libhailort "${_IMPORT_PREFIX}/lib/libhailort.so.5.2.0" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
