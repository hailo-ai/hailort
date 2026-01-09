cmake_minimum_required(VERSION 3.11.0)

include(${CMAKE_CURRENT_LIST_DIR}/protobuf.cmake)

find_package(PkgConfig REQUIRED)
pkg_check_modules(grpc REQUIRED IMPORTED_TARGET grpc++_unsecure)
