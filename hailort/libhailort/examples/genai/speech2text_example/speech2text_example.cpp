/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file speech2text_example.cpp
 **/

#include "hailo/genai/speech2text/speech2text.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>


hailort::Buffer get_input_audio_file(std::string &file_path)
{
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        throw hailort::hailort_error(HAILO_FILE_OPERATION_FAILURE, "Failed to open input-audio file");
    }
    auto file_size = std::filesystem::file_size(file_path);
    auto input_file_buffer = hailort::Buffer::create(file_size, hailort::BufferStorageParams::create_dma()).expect("Failed to create input file buffer");

    file.read(reinterpret_cast<char*>(input_file_buffer.data()), static_cast<std::streamsize>(file_size));
    return input_file_buffer;
}

int main(int argc, char **argv)
{
    try {
        if (3 != argc) {
            throw hailort::hailort_error(HAILO_INVALID_ARGUMENT, "Missing arguments!\nUsage: example <hef_path> <input_audio_file_path>");
        }
        std::string speech2text_hef_path = argv[1];
        std::string input_audio_file_path = argv[2];

        std::cout << "Starting Speech2Text...\n";
        auto vdevice = hailort::VDevice::create_shared().expect("Failed to create VDevice");

        auto speech2text_params = hailort::genai::Speech2TextParams(speech2text_hef_path);
        auto speech2text = hailort::genai::Speech2Text::create(vdevice, speech2text_params).expect("Failed to create Speech2Text");

        /*
            The audio file must be in format PCM float32 normalized [-1.0, 1.0), mono, (little-endian) at 16 kHz.
        */
        auto audio_chunk = get_input_audio_file(input_audio_file_path);

        auto speech2text_generator_params = speech2text.create_generator_params().expect("Failed to create generator params");
        speech2text_generator_params.set_task(hailort::genai::Speech2TextTask::TRANSCRIBE);
        speech2text_generator_params.set_language("en");

        auto segments = speech2text.generate_all_segments(hailort::MemoryView(audio_chunk), speech2text_generator_params).expect("Failed to generate");
        for (const auto &segment : segments) {
            std::cout << "[" << segment.start_sec << ", " << segment.end_sec << "] " << segment.text << std::endl;
        }
    } catch (const hailort::hailort_error &exception) {
        std::cout << "Failed to run speech2text. status=" << exception.status() << ", error message: " << exception.what() << std::endl;
        return -1;
    };

    return 0;
}
