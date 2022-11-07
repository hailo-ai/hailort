cmake_minimum_required(VERSION 3.0.0)

project(hailort-examples)

add_subdirectory(cpp)
add_subdirectory(c)

# We add a costum target in order to compile all of the hailort examples
add_custom_target(hailort_examples)
add_dependencies(hailort_examples c_hailort_examples cpp_hailort_examples)