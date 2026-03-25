cmake_minimum_required(VERSION 3.11)

project(hailopp-examples-mot17-demo)

find_package(HailoRT 5 REQUIRED)
find_package(HailoPP 1 REQUIRED)

set(EXAMPLE_NAME hailopp_mot17_demo)

add_executable(${EXAMPLE_NAME} mot17_demo.cpp)

target_link_libraries(${EXAMPLE_NAME} PRIVATE HailoPP::hailopp)
target_include_directories(${EXAMPLE_NAME} PRIVATE
    $<TARGET_PROPERTY:HailoRT::libhailort,INTERFACE_INCLUDE_DIRECTORIES>)

set_property(TARGET ${EXAMPLE_NAME} PROPERTY CXX_STANDARD 17)

if(WIN32)
    target_compile_options(${EXAMPLE_NAME} PRIVATE
        /DNOMINMAX
        /wd4251
    )
endif()

install(TARGETS ${EXAMPLE_NAME}
    RUNTIME DESTINATION bin/examples
)
