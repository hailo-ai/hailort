cmake_minimum_required(VERSION 3.0.0)
project(hailort_prebuild_external)

include(ExternalProject)

function(git_clone proj repo tag)
    ExternalProject_Add(git_clone_${proj}
        GIT_REPOSITORY  ${repo}
        GIT_TAG         ${tag}
        # GIT_SHALLOW     TRUE
        SOURCE_DIR      ${HAILO_EXTERNAL_DIR}/${proj}
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND ""
        TEST_COMMAND ""
    )
endfunction()

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/external/pybind11.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/external/catch2.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/external/spdlog.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/external/json.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/external/dotwriter.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/external/benchmark.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/external/readerwriterqueue.cmake)
if(CMAKE_SYSTEM_NAME STREQUAL QNX)
    include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/external/pevents.cmake)
endif()

git_clone(CLI11             https://github.com/hailo-ai/CLI11.git                                   f1644f15f219303b7ad670732c21018a1e6f0e11)
git_clone(protobuf          https://github.com/protocolbuffers/protobuf.git                         22d0e265de7d2b3d2e9a00d071313502e7d4cccf)

if(HAILO_BUILD_SERVICE)
    git_clone(grpc              https://github.com/grpc/grpc                                            53d69cc581c5b7305708587f4f1939278477c28a)
endif()
