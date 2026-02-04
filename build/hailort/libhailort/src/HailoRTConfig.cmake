
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was Config.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

if(TARGET libhailort)
    if(HailoRT_FIND_VERSION)
        # Extract major version from requested version
        string(REGEX REPLACE "^([0-9]+).*" "\\1" FIND_MAJOR_VER "${HailoRT_FIND_VERSION}")
        # Only create alias if major versions match
        if("${FIND_MAJOR_VER}" STREQUAL "${HAILORT_MAJOR_VERSION}")
            add_library(HailoRT::libhailort ALIAS libhailort)
        endif()
    endif()
else()
    include("${CMAKE_CURRENT_LIST_DIR}/HailoRTTargets.cmake")
    check_required_components(HailoRT)
endif()
