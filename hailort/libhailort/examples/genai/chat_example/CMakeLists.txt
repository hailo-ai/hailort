cmake_minimum_required(VERSION 3.5.0)

find_package(HailoRT 5.0.0 EXACT REQUIRED)

# Add nlohmann_json for chat-templates
include(FetchContent)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/ArthurSonzogni/nlohmann_json_cmake_fetchcontent.git
    GIT_TAG 391786c6c3abdd3eeb993a3154f1f2a4cfe137a0 # Version 3.9.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(json)

add_executable(chat_example chat.cpp)

target_link_libraries(chat_example PRIVATE HailoRT::libhailort nlohmann_json)

if(WIN32)
    target_compile_options(chat_example PRIVATE
        /DWIN32_LEAN_AND_MEAN
        /DNOMINMAX                  # NOMINMAX is required in order to play nice with std::min/std::max (otherwise Windows.h defines it's own)
        /wd4201 /wd4251
    )
endif()

set_target_properties(chat_example PROPERTIES CXX_STANDARD 14)