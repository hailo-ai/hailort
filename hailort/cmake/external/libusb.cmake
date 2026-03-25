cmake_minimum_required(VERSION 3.5.0)

# using pkgconfig because libusb does not provide a CMake configuration file
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
