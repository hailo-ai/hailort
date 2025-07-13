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

#include <nlohmann/json.hpp>

nlohmann::json parse_json(const std::string &json_path)
{
    std::ifstream file(json_path);
    nlohmann::json json_obj;
    file >> json_obj;

    return json_obj;
}

std::string get_user_prompt()
{
    std::cout << ">>> ";
    std::string prompt;
    getline(std::cin, prompt);
    return prompt;
}

std::string get_file_name(const std::string &path)
{
    // Split path to be the chars between the last '/' and the last '.'
    return path.substr(path.find_last_of('/') + 1, path.find_last_of('.') - path.find_last_of('/') - 1);
}

int main(int argc, char **argv)
{
    try {
        if (2 != argc) {
            throw hailort::hailort_error(HAILO_INVALID_ARGUMENT, "Missing HEF file path!\nUsage: example <hef_path>");
        }
        // Get HEF path from command line argument
        std::string llm_hef_path = argv[1];
        std::string llm_hef_name = get_file_name(llm_hef_path);

        // Parse prompts structure from JSON file
        const std::string json_path = "prompts_structure.json";
        auto json_obj = parse_json(json_path);
        const std::string first_prompt_prefix   = json_obj[llm_hef_name]["first_prompt_prefix"];
        const std::string general_prompt_prefix = json_obj[llm_hef_name]["general_prompt_prefix"];
        const std::string prompt_suffix         = json_obj[llm_hef_name]["prompt_suffix"];

        std::cout << "Starting LLM - " << llm_hef_name << "...\n";
        auto vdevice = hailort::VDevice::create_shared().expect("Failed to create VDevice");

        auto llm_params = hailort::genai::LLMParams();
        llm_params.set_model(llm_hef_path);
        auto llm = hailort::genai::LLM::create(vdevice, llm_params).expect("Failed to create LLM");

        std::string prompt_prefix = first_prompt_prefix;

        auto generator_params = llm.create_generator_params().expect("Failed to create generator params");
        auto generator = llm.create_generator(generator_params).expect("Failed to create generator");

        std::cout << "Enter prompt: (use Ctrl+C to exit)\n";
        while (true) {
            // Can call multiple writes before calling generate()
            auto status = generator.write(prompt_prefix + get_user_prompt() + prompt_suffix);

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

            prompt_prefix = general_prompt_prefix;

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
