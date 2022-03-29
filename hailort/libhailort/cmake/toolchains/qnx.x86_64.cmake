set(CMAKE_SYSTEM_NAME QNX)
set(arch ntox86_64)
set(QNX_PROCESSOR x86_64)

set(CMAKE_C_COMPILER $ENV{QNX_HOST}/usr/bin/${arch}-gcc)
set(CMAKE_C_COMPILER_TARGET ${arch})

set(CMAKE_CXX_COMPILER $ENV{QNX_HOST}/usr/bin/${arch}-g++)
set(CMAKE_CXX_COMPILER_TARGET ${arch})

add_definitions("-D_QNX_SOURCE")

file(GLOB_RECURSE libgcc_a 
  "$ENV{QNX_HOST}/usr/lib/gcc/${QNX_PROCESSOR}*/*/pic/libgcc.a")

set(CMAKE_C_STANDARD_LIBRARIES_INIT
  "${libgcc_a} -lc -lsocket -Bstatic -lcS")
set(CMAKE_CXX_STANDARD_LIBRARIES_INIT
  "-lc++ -lstdc++ -lm ${CMAKE_C_STANDARD_LIBRARIES_INIT}")

# pybind is not supported in this platform
set(HAILO_BUILD_PYBIND 0)
# GStreamer does not work on QNX currently
set(HAILO_BUILD_GSTREAMER 0)