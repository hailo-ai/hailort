cmake_minimum_required(VERSION 3.5.0)

add_subdirectory(chat_example)
add_subdirectory(text2image_example)
add_subdirectory(vlm_example)

set(GENAI_EXAMPLE_TARGETS
    chat_example
    image_generation_example
    vlm_example
)

add_custom_target(genai_hailort_examples)
add_dependencies(genai_hailort_examples ${GENAI_EXAMPLE_TARGETS})
