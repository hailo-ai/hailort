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

        auto vlm_params = hailort::genai::VLMParams(vlm_hef_path);
        auto vlm = hailort::genai::VLM::create(vdevice, vlm_params).expect("Failed to create VLM");

        while (true) {
            std::vector<hailort::MemoryView> input_frames;
            std::cout << "Enter frame path. for not using a frame, pass 'NONE' (use Ctrl+C to exit)\n";
            std::string frame_path = get_user_prompt();

            hailort::BufferPtr input_frame_buffer;
            if (frame_path != "NONE") {
                /*
                    The input-frame should be of shape 'vlm.input_frame_shape()', order 'vlm.input_frame_format_order()',
                    and type 'vlm.input_frame_format_type()'.
                */
                auto input_frame_size = vlm.input_frame_size();
                input_frame_buffer = hailort::Buffer::create_shared(input_frame_size).expect("Failed to allocate input frame buffer");
                get_input_frame(frame_path, *input_frame_buffer, input_frame_size);
                input_frames.push_back(hailort::MemoryView(*input_frame_buffer));
            }

            std::cout << "Enter input prompt\n";
            auto input_prompt = get_user_prompt();

            // Create structured messages using JSON format
            std::vector<std::string> messages;

            // Create user message with appropriate content
            std::string user_message;
            if (!input_frames.empty()) {
                // Message with both image and text
                user_message = R"({"role": "user", "content": [{"type": "image"}, {"type": "text", "text": ")" + input_prompt + R"("}]})";
            } else {
                // Text-only message
                user_message = R"({"role": "user", "content": ")" + input_prompt + R"("})";
            }
            messages.push_back(user_message);

            auto generator_completion = vlm.generate(messages, input_frames).expect("Failed to generate");

            while (hailort::genai::LLMGeneratorCompletion::Status::GENERATING == generator_completion.generation_status()) {
                auto output = generator_completion.read().expect("read failed!");
                std::cout << output << std::flush;
            }
            std::cout << std::endl;

            /*
                Since context is not cleared every message, the conversation continues naturally.
                To start a fresh conversation, call vlm.clear_context()
            */
            // vlm.clear_context();
        }
    } catch (const hailort::hailort_error &exception) {
        std::cout << "Failed to run vlm example. status=" << exception.status() << ", error message: " << exception.what() << std::endl;
        return -1;
    };

    return 0;
}
