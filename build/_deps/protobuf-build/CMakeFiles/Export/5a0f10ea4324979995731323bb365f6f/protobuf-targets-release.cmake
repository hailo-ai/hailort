#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "protobuf::libprotobuf-lite" for configuration "Release"
set_property(TARGET protobuf::libprotobuf-lite APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(protobuf::libprotobuf-lite PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libprotobuf-lite.a"
  )

list(APPEND _cmake_import_check_targets protobuf::libprotobuf-lite )
list(APPEND _cmake_import_check_files_for_protobuf::libprotobuf-lite "${_IMPORT_PREFIX}/lib/libprotobuf-lite.a" )

# Import target "protobuf::libprotobuf" for configuration "Release"
set_property(TARGET protobuf::libprotobuf APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(protobuf::libprotobuf PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libprotobuf.a"
  )

list(APPEND _cmake_import_check_targets protobuf::libprotobuf )
list(APPEND _cmake_import_check_files_for_protobuf::libprotobuf "${_IMPORT_PREFIX}/lib/libprotobuf.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
