/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file chat.cpp
 **/

#include "hailo/genai/llm/llm.hpp"
#include <iostream>
#include <fstream>

std::string get_user_prompt()
{
    std::cout << ">>> ";
    std::string prompt;
    getline(std::cin, prompt);
    return prompt;
}

int main(int argc, char **argv)
{
    try {
        if (2 != argc) {
            throw hailort::hailort_error(HAILO_INVALID_ARGUMENT, "Missing HEF file path!\nUsage: example <hef_path>");
        }
        std::string llm_hef_path = argv[1];

        std::cout << "Starting LLM - " << llm_hef_path << "...\n";
        auto vdevice = hailort::VDevice::create_shared().expect("Failed to create VDevice");

        auto llm_params = hailort::genai::LLMParams(llm_hef_path, "", true);
        auto llm = hailort::genai::LLM::create(vdevice, llm_params).expect("Failed to create LLM");

        std::cout << "Enter prompt: (use Ctrl+C to exit)\n";

        while (true) {
            std::string user_input = get_user_prompt();
            std::vector<std::string> prompt_json_strings = {
                R"({"role": "user", "content": ")" + user_input + R"("})"
            };
            auto generator_completion = llm.generate(prompt_json_strings).expect("Failed to generate");

            while (hailort::genai::LLMGeneratorCompletion::Status::GENERATING == generator_completion.generation_status()) {
                auto output = generator_completion.read().expect("read failed!");
                std::cout << output << std::flush;
            }
            std::cout << std::endl;

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
