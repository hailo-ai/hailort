set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -llog")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -llog")

set(ANDROID_ABI "arm64-v8a")
set(ANDROID_PLATFORM "android-28")

if(NOT DEFINED ENV{ANDROID_NDK})
    message(FATAL_ERROR "ANDROID_NDK env variable must be set. Please install android ndk and set this variable.")
endif()

include("$ENV{ANDROID_NDK}/build/cmake/android.toolchain.cmake")
