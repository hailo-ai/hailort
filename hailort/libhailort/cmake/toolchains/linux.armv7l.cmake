set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-linux-gnueabi-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabi-g++)
set(CMAKE_STRIP arm-linux-gnueabi--strip CACHE FILEPATH "Strip")
set(CMAKE_LINKER arm-linux-gnueabi-ld)

add_compile_options(-march=armv7-a)

# pybind is not supported in this platform
set(HAILO_BUILD_PYBIND 0)
