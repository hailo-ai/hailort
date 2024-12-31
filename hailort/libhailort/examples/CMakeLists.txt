cmake_minimum_required(VERSION 3.5.0)

project(hailort-examples)

if(WIN32)
    add_compile_options(/W4)
elseif(UNIX)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "QCC")
        add_compile_options(-Wall -Wextra -Wconversion)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-Wall -Wextra -Wconversion -Wno-missing-braces)
    endif()
else()
    message(FATAL_ERROR "Unexpeced host, stopping build")
endif()

if (HAILO_COMPILE_WARNING_AS_ERROR)
    # Treat warnings as errors for all examples
    if(WIN32)
        add_compile_options(/WX)
    elseif(UNIX)
        add_compile_options(-Werror)
    else()
        message(FATAL_ERROR "Unexpeced host, stopping build")
    endif()
endif()

add_subdirectory(cpp)
add_subdirectory(c)

# We add a costum target in order to compile all of the hailort examples
add_custom_target(hailort_examples)
add_dependencies(hailort_examples c_hailort_examples cpp_hailort_examples)
