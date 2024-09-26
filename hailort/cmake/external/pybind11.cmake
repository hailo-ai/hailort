cmake_minimum_required(VERSION 3.11.0)

include(FetchContent)

FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG 80dc998efced8ceb2be59756668a7e90e8bef917 # Version 2.10.1
    GIT_SHALLOW TRUE
    SOURCE_DIR ${HAILO_EXTERNAL_DIR}/pybind11-src
    SUBBUILD_DIR ${HAILO_EXTERNAL_DIR}/pybind11-subbuild
)

# https://stackoverflow.com/questions/65527126/disable-install-for-fetchcontent
FetchContent_GetProperties(pybind11)
if(NOT pybind11_POPULATED)
    FetchContent_Populate(pybind11)
    if (NOT HAILO_EXTERNALS_EXCLUDE_TARGETS)
        if(NOT PYTHON_EXECUTABLE AND PYBIND11_PYTHON_VERSION)
            # venv version is prioritized (instead of PYBIND11_PYTHON_VERSION) if PYTHON_EXECUTABLE is not set.
            # See https://pybind11.readthedocs.io/en/stable/changelog.html#v2-6-0-oct-21-2020
            if((${CMAKE_VERSION} VERSION_LESS "3.22.0") AND (NOT WIN32))
                find_package(PythonInterp ${PYBIND11_PYTHON_VERSION} REQUIRED)
                set(PYTHON_EXECUTABLE ${Python_EXECUTABLE})
            else()
                find_package(Python3 ${PYBIND11_PYTHON_VERSION} REQUIRED EXACT COMPONENTS Interpreter Development)
                set(PYTHON_EXECUTABLE ${Python3_EXECUTABLE})
            endif()
        endif()
        add_subdirectory(${pybind11_SOURCE_DIR} ${pybind11_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
endif()
