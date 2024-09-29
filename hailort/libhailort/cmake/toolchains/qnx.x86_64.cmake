# CMake added fix for QCC compiler in this version - will not compile in older versions
cmake_minimum_required(VERSION 3.14.0)

set(CMAKE_SYSTEM_NAME QNX)
set(QNX_PROCESSOR x86_64)
SET(CMAKE_SYSTEM_PROCESSOR x86_64)

SET(CMAKE_MAKE_PROGRAM "$ENV{QNX_HOST}/usr/bin/make"                                  CACHE PATH "QNX Make Program")
SET(CMAKE_SH           "$ENV{QNX_HOST}/usr/bin/sh "                                   CACHE PATH "QNX shell Program")
SET(CMAKE_AR           "$ENV{QNX_HOST}/usr/bin/nto${CMAKE_SYSTEM_PROCESSOR}-ar"       CACHE PATH "QNX ar Program")
SET(CMAKE_RANLIB       "$ENV{QNX_HOST}/usr/bin/nto${CMAKE_SYSTEM_PROCESSOR}-ranlib"   CACHE PATH "QNX ranlib Program")
SET(CMAKE_NM           "$ENV{QNX_HOST}/usr/bin/nto${CMAKE_SYSTEM_PROCESSOR}-nm"       CACHE PATH "QNX nm Program")
SET(CMAKE_OBJCOPY      "$ENV{QNX_HOST}/usr/bin/nto${CMAKE_SYSTEM_PROCESSOR}-objcopy"  CACHE PATH "QNX objcopy Program")
SET(CMAKE_OBJDUMP      "$ENV{QNX_HOST}/usr/bin/nto${CMAKE_SYSTEM_PROCESSOR}-objdump"  CACHE PATH "QNX objdump Program")
SET(CMAKE_LINKER       "$ENV{QNX_HOST}/usr/bin/nto${CMAKE_SYSTEM_PROCESSOR}-ld"       CACHE PATH "QNX Linker Program")
SET(CMAKE_STRIP        "$ENV{QNX_HOST}/usr/bin/nto${CMAKE_SYSTEM_PROCESSOR}-strip"    CACHE PATH "QNX Strip Program")

add_definitions("-D_QNX_SOURCE")

SET(CMAKE_SHARED_LIBRARY_PREFIX "lib")
SET(CMAKE_SHARED_LIBRARY_SUFFIX ".so")
SET(CMAKE_STATIC_LIBRARY_PREFIX "lib")
SET(CMAKE_STATIC_LIBRARY_SUFFIX ".a")

SET(CMAKE_C_COMPILER $ENV{QNX_HOST}/usr/bin/qcc)
SET(CMAKE_C_FLAGS_DEBUG "-g")
SET(CMAKE_C_FLAGS_MINSIZEREL "-Os -DNDEBUG")
SET(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")
SET(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g")

SET(CMAKE_CXX_COMPILER $ENV{QNX_HOST}/usr/bin/q++)
SET(CMAKE_CXX_FLAGS_DEBUG "-g")
SET(CMAKE_CXX_FLAGS_MINSIZEREL "-Os -DNDEBUG")
SET(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")

LIST(APPEND CMAKE_FIND_ROOT_PATH $ENV{QNX_TARGET})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

SET(CMAKE_C_FLAGS "-Vgcc_nto${QNX_PROCESSOR}" CACHE STRING "qcc c flags" FORCE)
SET(CMAKE_CXX_FLAGS "-Vgcc_nto${QNX_PROCESSOR} -lang-c++ -Y_cxx" CACHE STRING "qcc cxx flags" FORCE)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--build-id=md5 -lang-c++ -lsocket ${EXTRA_CMAKE_LINKER_FLAGS}" CACHE STRING "exe_linker_flags")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--build-id=md5 -lang-c++ -lsocket ${EXTRA_CMAKE_LINKER_FLAGS}" CACHE STRING "so_linker_flags")

# GStreamer does not work on QNX currently
set(HAILO_BUILD_GSTREAMER "OFF" CACHE STRING "hailo_build_gstreamer" FORCE)
# Hailort service does not work on QNX currently
set(HAILO_BUILD_SERVICE "OFF" CACHE STRING "hailo_build_service" FORCE)
# Set little endian flag for protobuf to work correctly on QNX
add_definitions("-D__LITTLE_ENDIAN__")
