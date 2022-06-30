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

git_clone(pybind11          https://github.com/pybind/pybind11.git                                  8de7772cc72daca8e947b79b83fea46214931604)
git_clone(Catch2            https://github.com/catchorg/Catch2.git                                  c4e3767e265808590986d5db6ca1b5532a7f3d13)
git_clone(CLI11             https://github.com/CLIUtils/CLI11.git                                   706b14fb14444e68ace232eb9a0ef106bf99343b)
git_clone(spdlog            https://github.com/gabime/spdlog.git                                    e2789531912a5c6ab28a90387f97c52963eec08a)
git_clone(protobuf          https://github.com/protocolbuffers/protobuf.git                         d0bfd5221182da1a7cc280f3337b5e41a89539cf)
git_clone(readerwriterqueue https://github.com/cameron314/readerwriterqueue.git                     435e36540e306cac40fcfeab8cc0a22d48464509)
git_clone(json              https://github.com/ArthurSonzogni/nlohmann_json_cmake_fetchcontent.git  391786c6c3abdd3eeb993a3154f1f2a4cfe137a0)
git_clone(DotWriter         https://github.com/hailo-ai/DotWriter.git                               e5fa8f281adca10dd342b1d32e981499b8681daf)
git_clone(benchmark         https://github.com/google/benchmark.git                                 f91b6b42b1b9854772a90ae9501464a161707d1e)
git_clone(pevents           https://github.com/neosmart/pevents.git                                 1209b1fd1bd2e75daab4380cf43d280b90b45366)
git_clone(microprofile      https://github.com/jonasmr/microprofile                                 03c34f96840defe0f4c196309628815d02b98059)
