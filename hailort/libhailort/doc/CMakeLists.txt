cmake_minimum_required(VERSION 3.5.0)

find_package(Doxygen)
if(DOXYGEN_FOUND)
    set(DOXYGEN_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/doxygen)
    set(DOXYGEN_INDEX_FILE ${DOXYGEN_OUTPUT_DIR}/xml/index.xml)
    set(DOXYFILE_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
    get_target_property(HAILORT_PUBLIC_HEADERS libhailort PUBLIC_HEADER)
    string (REPLACE ";" " " HAILORT_PUBLIC_HEADERS_SPACE_SEPARATED "${HAILORT_PUBLIC_HEADERS}")

    # Replace variables inside @@ with the current values
    configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} @ONLY)
    # Doxygen won't create this for us
    file(MAKE_DIRECTORY ${DOXYGEN_OUTPUT_DIR})

    # Only regenerate Doxygen when the Doxyfile or public headers change
    add_custom_command(OUTPUT ${DOXYGEN_INDEX_FILE}
                      DEPENDS ${HAILORT_PUBLIC_HEADERS}
                      COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
                      MAIN_DEPENDENCY ${DOXYFILE_OUT} ${DOXYFILE_IN}
                      COMMENT "Generating doc")

    add_custom_target(doxygen DEPENDS ${DOXYGEN_INDEX_FILE})

    install(
        DIRECTORY "${DOXYGEN_OUTPUT_DIR}" DESTINATION "doc/"
        CONFIGURATIONS Release)

    add_custom_target(doc ALL DEPENDS doxygen)

endif()