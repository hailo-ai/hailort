# Install script for directory: /home/jordan/hailort/hailort/libhailort/src

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/hailo" TYPE DIRECTORY FILES "/home/jordan/hailort/hailort/libhailort/include/hailo/")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libhailort.so.5.2.0" AND
       NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libhailort.so.5.2.0")
      file(RPATH_CHECK
           FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libhailort.so.5.2.0"
           RPATH "")
    endif()
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE SHARED_LIBRARY FILES "/home/jordan/hailort/build/hailort/libhailort/src/libhailort.so.5.2.0")
    if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libhailort.so.5.2.0" AND
       NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libhailort.so.5.2.0")
      if(CMAKE_INSTALL_DO_STRIP)
        execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libhailort.so.5.2.0")
      endif()
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE SHARED_LIBRARY FILES "/home/jordan/hailort/build/hailort/libhailort/src/libhailort.so")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  execute_process(COMMAND ldconfig)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "libhailort" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/HailoRT" TYPE FILE FILES
    "/home/jordan/hailort/build/hailort/libhailort/src/HailoRTConfig.cmake"
    "/home/jordan/hailort/build/hailort/libhailort/src/HailoRTConfigVersion.cmake"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "libhailort" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/HailoRT/HailoRTTargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/HailoRT/HailoRTTargets.cmake"
         "/home/jordan/hailort/build/hailort/libhailort/src/CMakeFiles/Export/2889dedff7f789699a09a5260145dc0f/HailoRTTargets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/HailoRT/HailoRTTargets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/HailoRT/HailoRTTargets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/HailoRT" TYPE FILE FILES "/home/jordan/hailort/build/hailort/libhailort/src/CMakeFiles/Export/2889dedff7f789699a09a5260145dc0f/HailoRTTargets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/HailoRT" TYPE FILE FILES "/home/jordan/hailort/build/hailort/libhailort/src/CMakeFiles/Export/2889dedff7f789699a09a5260145dc0f/HailoRTTargets-release.cmake")
  endif()
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("/home/jordan/hailort/build/hailort/libhailort/src/utils/cmake_install.cmake")
  include("/home/jordan/hailort/build/hailort/libhailort/src/os/cmake_install.cmake")
  include("/home/jordan/hailort/build/hailort/libhailort/src/device_common/cmake_install.cmake")
  include("/home/jordan/hailort/build/hailort/libhailort/src/device/cmake_install.cmake")
  include("/home/jordan/hailort/build/hailort/libhailort/src/vdevice/cmake_install.cmake")
  include("/home/jordan/hailort/build/hailort/libhailort/src/transform/cmake_install.cmake")
  include("/home/jordan/hailort/build/hailort/libhailort/src/stream_common/cmake_install.cmake")
  include("/home/jordan/hailort/build/hailort/libhailort/src/vdma/cmake_install.cmake")
  include("/home/jordan/hailort/build/hailort/libhailort/src/hef/cmake_install.cmake")
  include("/home/jordan/hailort/build/hailort/libhailort/src/network_group/cmake_install.cmake")
  include("/home/jordan/hailort/build/hailort/libhailort/src/core_op/cmake_install.cmake")
  include("/home/jordan/hailort/build/hailort/libhailort/src/net_flow/cmake_install.cmake")
  include("/home/jordan/hailort/build/hailort/libhailort/src/rpc_callbacks/cmake_install.cmake")
  include("/home/jordan/hailort/build/hailort/libhailort/src/genai/cmake_install.cmake")
  include("/home/jordan/hailort/build/hailort/libhailort/src/perfetto/cmake_install.cmake")

endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/jordan/hailort/build/hailort/libhailort/src/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
