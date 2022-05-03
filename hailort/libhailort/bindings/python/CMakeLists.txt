add_subdirectory(src)

# copy files to venv
if(HAILO_BUILD_PYHAILORT_VENV)
    add_custom_target(pyhailort_venv ALL
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:_pyhailort> ${CMAKE_CURRENT_LIST_DIR}/platform/hailo_platform/pyhailort/
    )
    add_dependencies(pyhailort_venv _pyhailort)
endif()