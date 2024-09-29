cmake_minimum_required(VERSION 3.5.0)

function(execute_process_in_clean_env)
    cmake_parse_arguments(execute_process_in_clean_env "" "RESULT_VARIABLE" "" ${ARGN})
    if(CMAKE_HOST_UNIX)
        string(REPLACE ";" "' '" cmdline "'${execute_process_in_clean_env_UNPARSED_ARGUMENTS}'")
        execute_process(COMMAND env -i HOME=$ENV{HOME} PATH=/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin bash -l -c "${cmdline}" OUTPUT_QUIET RESULT_VARIABLE result)
    else()
        # TODO: make it clean env for cross compile
        set(cmdline ${execute_process_in_clean_env_UNPARSED_ARGUMENTS})
        execute_process(COMMAND ${cmdline} OUTPUT_QUIET RESULT_VARIABLE result)
    endif()
    if(DEFINED ${execute_process_in_clean_env_RESULT_VARIABLE})
        set(${execute_process_in_clean_env_RESULT_VARIABLE} ${result} PARENT_SCOPE)
    endif()
endfunction()

function(execute_cmake)
    set(options PARALLEL_BUILD)
    set(oneValueArgs SOURCE_DIR BUILD_DIR)
    set(multiValueArgs CONFIGURE_ARGS BUILD_ARGS)
    cmake_parse_arguments(execute_cmake "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    execute_process_in_clean_env(
        ${CMAKE_COMMAND}
        ${execute_cmake_SOURCE_DIR}
        -B ${execute_cmake_BUILD_DIR}
        -G "${CMAKE_GENERATOR}"
        ${execute_cmake_CONFIGURE_ARGS}
        RESULT_VARIABLE result
    )
    if(result)
        message(FATAL_ERROR "Failed configuring ${execute_cmake_SOURCE_DIR}")
    endif()

    if(${execute_cmake_PARALLEL_BUILD} AND (CMAKE_GENERATOR MATCHES "Unix Makefiles"))
        execute_process(COMMAND grep -c ^processor /proc/cpuinfo OUTPUT_VARIABLE cores_count RESULT_VARIABLE result)
        string(STRIP ${cores_count} cores_count)
        if(result)
            message(FATAL_ERROR "Failed getting the amount of cores")
        endif()
        set(CMAKE_EXTRA_BUILD_ARGS -- -j${cores_count})
    endif()

    execute_process_in_clean_env(
        "${CMAKE_COMMAND}" --build "${execute_cmake_BUILD_DIR}" "${execute_cmake_BUILD_ARGS}" ${CMAKE_EXTRA_BUILD_ARGS}
        RESULT_VARIABLE result
    )
    if(result)
        message(FATAL_ERROR "Failed building ${execute_cmake_SOURCE_DIR}")
    endif()
endfunction()