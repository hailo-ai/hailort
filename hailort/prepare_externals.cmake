cmake_minimum_required(VERSION 3.5.0)

# TODO: remove execute_cmake. support script mode?
execute_process(COMMAND
    ${CMAKE_COMMAND}
    -S ${CMAKE_CURRENT_LIST_DIR}/prepare_externals
    -B ${CMAKE_CURRENT_LIST_DIR}/prepare_externals/build
    -G "${CMAKE_GENERATOR}"
    -DHAILO_EXTERNAL_DIR=${CMAKE_CURRENT_LIST_DIR}/external
)
