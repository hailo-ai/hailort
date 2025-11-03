cmake_minimum_required(VERSION 3.14)

include(FetchContent)

FetchContent_Declare(
    tokenizers
    GIT_REPOSITORY https://github.com/mlc-ai/tokenizers-cpp.git
    GIT_TAG 55d53aa38dc8df7d9c8bd9ed50907e82ae83ce66 # main
    # GIT_SHALLOW TRUE
    SOURCE_DIR ${HAILO_EXTERNAL_DIR}/tokenizers-src
    SUBBUILD_DIR ${HAILO_EXTERNAL_DIR}/tokenizers-subbuild
)

# https://stackoverflow.com/questions/61499646/cmake-set-variable-readonly-protect-from-override
macro(set_readonly VAR)
  # Set the variable itself
  set("${VAR}" "${ARGN}")
  # Store the variable's value for restore it upon modifications.
  set("_${VAR}_readonly_val" "${ARGN}")
  # Register a watcher for a variable
  variable_watch("${VAR}" readonly_guard)
endmacro()

# Watcher for a variable which emulates readonly property.
macro(readonly_guard VAR access value current_list_file stack)
  if ("${access}" STREQUAL "MODIFIED_ACCESS")
    message(WARNING "Attempt to change readonly variable '${VAR}'!")
    # Restore a value of the variable to the initial one.
    set(${VAR} "${_${VAR}_readonly_val}")
  endif()
endmacro()

# On kirkstone-builds we have an issue with compiling tokenizers_cpp, so we support getting .a path
option(TOKENIZERS_LIB_PATH "Path to tokenizers_cpp library" "")
option(TOKENIZERS_RUST_LIB_PATH "Path to tokenizers_cpp rust library" "")
option(TOKENIZERS_INCLUDE_DIR "Path to include dir of tokenizers_cpp" "")
if (TOKENIZERS_LIB_PATH AND TOKENIZERS_RUST_LIB_PATH AND TOKENIZERS_INCLUDE_DIR)
  message(STATUS "Will link against given tokenizers: ${TOKENIZERS_LIB_PATH}")
  message(STATUS "Will link against given tokenizers rust: ${TOKENIZERS_RUST_LIB_PATH}")
  message(STATUS "Will include given include dir:     ${TOKENIZERS_INCLUDE_DIR}")

  # Create an imported target for the static library
  if(NOT TARGET tokenizers_cpp)
    add_library(tokenizers_cpp STATIC IMPORTED)
    # Set the properties of the imported library
    set_target_properties(tokenizers_cpp PROPERTIES
        IMPORTED_LOCATION ${TOKENIZERS_LIB_PATH}
        INTERFACE_INCLUDE_DIRECTORIES ${TOKENIZERS_INCLUDE_DIR}
    )
    if(NOT MSVC)
      set_target_properties(tokenizers_cpp PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()

    # Linking with dl is required for the rust impl of tokenizers - https://github.com/mlc-ai/tokenizers-cpp/issues/1#issuecomment-1556206254
    target_link_libraries(tokenizers_cpp INTERFACE ${TOKENIZERS_RUST_LIB_PATH} dl)
  endif()
else()
  # https://stackoverflow.com/questions/65527126/disable-install-for-fetchcontent
  FetchContent_GetProperties(tokenizers)
  if(NOT tokenizers_POPULATED)
      FetchContent_Populate(tokenizers)
      if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
          set_readonly(TOKENIZERS_CPP_CARGO_TARGET x86_64-unknown-linux-gnu)
      elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
          if(ANDROID)
              set_readonly(TOKENIZERS_CPP_CARGO_TARGET aarch64-linux-android)
          else()
              set_readonly(TOKENIZERS_CPP_CARGO_TARGET aarch64-unknown-linux-gnu)
          endif()
      endif()
      set(MLC_ENABLE_SENTENCEPIECE_TOKENIZER OFF) # Disable sentencepiece for reducing binary size
      if (NOT HAILO_EXTERNALS_EXCLUDE_TARGETS)
          # This step requires cargo to be installed
          find_program(CARGO_EXECUTABLE cargo)
          if (NOT CARGO_EXECUTABLE)
              message(FATAL_ERROR "Cargo is not installed or not found in PATH.")
          endif()
          add_subdirectory(${tokenizers_SOURCE_DIR} ${tokenizers_BINARY_DIR} EXCLUDE_FROM_ALL)
      endif()
      if(NOT MSVC)
        set_target_properties(tokenizers_cpp PROPERTIES POSITION_INDEPENDENT_CODE ON)
      endif()
  endif()
endif()
