cmake_minimum_required(VERSION 3.5.0)

project(hailort-examples-genai)

add_subdirectory(chat_example)
add_subdirectory(vlm_example)
add_subdirectory(speech2text_example)

set(GENAI_EXAMPLE_TARGETS
    chat_example
    vlm_example
    speech2text_example
)

add_custom_target(genai_hailort_examples)
add_dependencies(genai_hailort_examples ${GENAI_EXAMPLE_TARGETS})
