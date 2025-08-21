/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file image_generation_example.cpp
 **/

#include "hailo/genai/text2image/text2image.hpp"
#include <iostream>
#include <fstream>


constexpr uint32_t TEXT2IMAGE_SEED_VALUE = 173;
const std::string NEGATIVE_PROMPT = "ugly, tiling, poorly drawn hands, poorly drawn feet, poorly drawn face, out of frame,extra limbs, disfigured, deformed, body out of frame, bad anatomy, watermark, signature,cut off, low contrast, underexposed, overexposed, bad art, beginner, amateur, distorted face";

std::string get_positive_prompt()
{
    std::cout << "Enter positive prompt" << std::endl;
    std::cout << ">>> ";
    std::string prompt;
    getline(std::cin, prompt);
    return prompt;
}

int main(int argc, char **argv)
{
    try {
        if (4 != argc) {
            throw hailort::hailort_error(HAILO_INVALID_ARGUMENT,
                "Missing HEFs files!\nUsage: example <denoiser_hef_path> (UNET) <image_decoder_hef_path> (VAE) and <text_encoder_hef_path> (CLIP)");
        }
        const std::string denoiser_hef_path = argv[1];
        const std::string image_decoder_hef_path = argv[2];
        const std::string text_encoder_hef_path = argv[3];

        std::cout << "Initializing Text2Image..." << std::endl;
        auto vdevice = hailort::VDevice::create_shared().expect("Failed to create VDevice");

        auto params = hailort::genai::Text2ImageParams();
        (void)params.set_denoise_model(denoiser_hef_path);
        (void)params.set_image_decoder_model(image_decoder_hef_path);
        (void)params.set_text_encoder_model(text_encoder_hef_path);

        auto text2image = hailort::genai::Text2Image::create(vdevice, params).expect("Failed to create Text2Image");

        while (true) {
            auto generator_params = text2image.create_generator_params().expect("Failed to create generator params");
            (void)generator_params.set_seed(TEXT2IMAGE_SEED_VALUE);

            auto positive_prompt = get_positive_prompt();

            std::cout << "Generating image... This may take a moment" << std::endl;
            auto images = text2image.generate(generator_params, positive_prompt, NEGATIVE_PROMPT).expect("Failed to generate");

            const std::string output_path = "generated_image.bin";
            // Save the generated image to a file
            std::ofstream output_file(output_path, std::ios::binary);
            output_file.write(reinterpret_cast<const char*>(images[0].data()), static_cast<std::streamsize>(images[0].size()));

            /*
                The generated image will be of shape 'text2image.output_sample_shape()', order 'text2image.output_sample_format_order()',
                and type 'text2image.output_sample_format_type()'
            */
            std::cout << "Image generation completed successfully! RGB Image is saved to " << output_path
                << " at resolution 512x512x3" << std::endl;
        }
    } catch (const hailort::hailort_error &exception) {
        std::cout << "Failed to run text to image generation. status=" << exception.status() << ", error message: " << exception.what() << std::endl;
        return -1;
    };

    return 0;
}