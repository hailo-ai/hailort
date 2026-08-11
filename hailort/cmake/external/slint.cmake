cmake_minimum_required(VERSION 3.11.0)

include(FetchContent)

FetchContent_Declare(
    slint
    GIT_REPOSITORY https://github.com/slint-ui/slint.git
    GIT_TAG e5754fc1c0e7e1ba4933ec0611afe076955a8791 # Version 1.15.1
    GIT_SHALLOW TRUE
    SOURCE_DIR ${HAILO_EXTERNAL_DIR}/slint-src
    SUBBUILD_DIR ${HAILO_EXTERNAL_DIR}/slint-subbuild
)

FetchContent_GetProperties(slint)
if(NOT slint_POPULATED)
    # Suppress dangerous_implicit_autorefs lint (deny-by-default in Rust 1.89+)
    # that Slint's generated Rust code may trigger with newer toolchains.
    set(ENV{RUSTFLAGS} "$ENV{RUSTFLAGS} -A dangerous_implicit_autorefs")

    FetchContent_Populate(slint)
    if(NOT HAILO_EXTERNALS_EXCLUDE_TARGETS)
        # Disable unused Slint features to speed up the build.
        set(SLINT_FEATURE_INTERPRETER OFF CACHE BOOL "" FORCE)
        set(SLINT_FEATURE_TESTING OFF CACHE BOOL "" FORCE)
        set(SLINT_FEATURE_ACCESSIBILITY OFF CACHE BOOL "" FORCE)
        set(SLINT_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        # FemtoVG renders gradients, paths, and border-radius correctly via OpenGL;
        # the software renderer flattens gradient brushes to a single color and
        # doesn't mask gradient backgrounds to border-radius.
        set(SLINT_FEATURE_RENDERER_FEMTOVG ON CACHE BOOL "" FORCE)
        set(SLINT_FEATURE_RENDERER_SOFTWARE ON CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(CPACK_GENERATOR "")

        add_subdirectory(${slint_SOURCE_DIR} ${slint_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
endif()
