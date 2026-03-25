cmake_minimum_required(VERSION 3.11)

project(hailopp-examples)

find_package(HailoRT 5 REQUIRED)
find_package(HailoPP 1 REQUIRED)

add_subdirectory(hailopp)

add_custom_target(hailopp_examples)
add_dependencies(hailopp_examples hailopp_hailopp_examples)
