/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vlm_example.cpp
 **/

#include "hailo/genai/vlm/vlm.hpp"
#include <iostream>
#include <fstream>

std::string get_user_prompt()
{
    std::cout << ">>> ";
    std::string prompt;
    getline(std::cin, prompt);
    return prompt;
}

void get_input_frame(std::string &frame_path, hailort::Buffer &input_frame_buffer, uint32_t input_frame_size)
{
    std::ifstream file(frame_path, std::ios::binary);
    if (!file) {
        throw hailort::hailort_error(HAILO_FILE_OPERATION_FAILURE, "Failed to open input-frame file");
    }
    file.read(reinterpret_cast<char*>(input_frame_buffer.data()), input_frame_size);
    if (file.gcount() != input_frame_size) {
        throw hailort::hailort_error(HAILO_FILE_OPERATION_FAILURE, "Failed to read input-frame file - input size mismatch (Expected: " +
            std::to_string(input_frame_size) + ", actual: " + std::to_string(file.gcount()) + ")");
    }
}

int main(int argc, char **argv)
{
    try {
        if (2 != argc) {
            throw hailort::hailort_error(HAILO_INVALID_ARGUMENT, "Missing HEF file path!\nUsage: example <hef_path>");
        }
        const std::string vlm_hef_path = argv[1];
        std::cout << "Starting VLM...\n";
        auto vdevice = hailort::VDevice::create_shared().expect("Failed to create VDevice");

        auto vlm_params = hailort::genai::VLMParams();
        vlm_params.set_model(vlm_hef_path);
        auto vlm = hailort::genai::VLM::create(vdevice, vlm_params).expect("Failed to create VLM");

        auto input_frame_size = vlm.input_frame_size();

        std::string prompt_prefix = "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\n";
        const std::string prompt_suffix = "<|im_end|>\n<|im_start|>assistant\n";
        const std::string vision_prompt = "<|vision_start|><|vision_pad|><|vision_end|>";

        auto input_frame_buffer = hailort::Buffer::create(input_frame_size).expect("Failed to allocate input frame buffer");
        auto generator_params = vlm.create_generator_params().expect("Failed to create generator params");
        auto generator = vlm.create_generator(generator_params).expect("Failed to create generator");
        while (true) {
            auto local_prompt_prefix = prompt_prefix;
            std::vector<hailort::MemoryView> input_frames;
            std::cout << "Enter frame path. for not using a frame, pass 'NONE' (use Ctrl+C to exit)\n";
            std::string frame_path = get_user_prompt();
            if (frame_path != "NONE") {
                /*
                    The input-frame should be of shape 'vlm.input_frame_shape()', order 'vlm.input_frame_format_order()',
                    and type 'vlm.input_frame_format_type()'.
                */
                get_input_frame(frame_path, input_frame_buffer, input_frame_size);
                input_frames.push_back(hailort::MemoryView(input_frame_buffer));
                local_prompt_prefix = local_prompt_prefix + vision_prompt;
            }
            std::cout << "Enter input prompt\n";
            auto input_prompt = get_user_prompt();

            auto generator_completion = generator.generate(local_prompt_prefix + input_prompt + prompt_suffix, input_frames).expect("Failed to generate");

            while (hailort::genai::LLMGeneratorCompletion::Status::GENERATING == generator_completion.generation_status()) {
                auto output = generator_completion.read().expect("read failed!");
                std::cout << output << std::flush;
            }
            std::cout << std::endl;

            /*
                Since context is not cleared every message, overridie 'prompt_prefix' to ensure 'system-prompt' is only applied once.
                If clearing context, this override is not needed
            */

            prompt_prefix = "<|im_start|>user\n";

            /*
                Clear context if necessary
            */
            // status = vlm.clear_context();
        }

    } catch (const hailort::hailort_error &exception) {
        std::cout << "Failed to run vlm example. status=" << exception.status() << ", error message: " << exception.what() << std::endl;
        return -1;
    };

    return 0;
}
