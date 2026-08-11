/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file chat_raw_prompt.cpp
 **/

#include "hailo/genai/llm/llm.hpp"
#include <iostream>

// Qwen2.5-1.5B-Instruct model constants
// Modify these constants to use a different model
static const std::string MODEL_HEF_FILE_PATH = "Qwen2.5-1.5B-Instruct.hef";
static const std::string MODEL_FIRST_PROMPT_PREFIX = "<|im_start|>system\nYou are a helpful assistant<|im_end|>\n<|im_start|>user\n";
static const std::string MODEL_GENERAL_PROMPT_PREFIX = "<|im_start|>user\n";
static const std::string MODEL_PROMPT_SUFFIX = "<|im_end|>\n<|im_start|>assistant\n";

bool get_user_prompt(std::string &prompt)
{
    std::cout << ">>> ";
    if (!getline(std::cin, prompt) || ("exit" == prompt) || ("quit" == prompt)) {
        return false;
    }
    return true;
}

int main()
{
    try {
        std::cout << "Starting LLM - " << MODEL_HEF_FILE_PATH << "...\n";
        auto vdevice = hailort::VDevice::create_shared().expect("Failed to create VDevice");

        auto llm_params = hailort::genai::LLMParams(MODEL_HEF_FILE_PATH);
        auto llm = hailort::genai::LLM::create(vdevice, llm_params).expect("Failed to create LLM");
        auto generator = llm.create_generator().expect("Failed to create generator");

        std::cout << "Enter prompt (type 'exit', 'quit' or Ctrl+D to exit):\n";

        std::string prompt_prefix = MODEL_FIRST_PROMPT_PREFIX;
        while (true) {
            std::string user_input;
            if (!get_user_prompt(user_input)) {
                std::cout << "Exiting...\n";
                break;
            }
            auto input_prompt = prompt_prefix + user_input + MODEL_PROMPT_SUFFIX;
            auto status = generator.write(input_prompt);
            if (HAILO_SUCCESS != status) {
                throw hailort::hailort_error(status, "Failed to write prompt");
            }

            auto generator_completion = generator.generate().expect("Failed to generate");
            while (hailort::genai::LLMGeneratorCompletion::Status::GENERATING == generator_completion.generation_status()) {
                auto output = generator_completion.read().expect("read failed!");
                std::cout << output << std::flush;
            }
            std::cout << std::endl;

            /*
                Since context is not cleared every message, override 'prompt_prefix' to ensure 'first_prompt_prefix' is only applied once.
                If clearing context, this override is not needed.
            */
            prompt_prefix = MODEL_GENERAL_PROMPT_PREFIX;

            /*
                Clear context if necessary
            */
            // status = llm.clear_context();
        }
    } catch (const hailort::hailort_error &exception) {
        std::cout << "Failed to run chat. status=" << exception.status() << ", error message: " << exception.what() << std::endl;
        return -1;
    };

    return 0;
}
