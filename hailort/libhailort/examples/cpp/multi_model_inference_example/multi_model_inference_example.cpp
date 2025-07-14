/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file multi_model_inference_example.cpp
 * This example demonstrates the concurrent inference of multiple networks using the HailoRT library.
 **/

#include "hailo/hailort.hpp"

#include <chrono>
#include <iostream>

#if defined(__unix__)
#include <sys/mman.h>
#endif

#define HEF_FILE ("hefs/shortcut_net.hef")

using namespace hailort;

static std::shared_ptr<uint8_t> page_aligned_alloc(size_t size)
{
#if defined(__unix__)
    auto addr = mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (MAP_FAILED == addr)
        throw std::bad_alloc();
    return std::shared_ptr<uint8_t>(reinterpret_cast<uint8_t *>(addr), [size](void *addr) { munmap(addr, size); });
#elif defined(_MSC_VER)
    auto addr = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!addr)
        throw std::bad_alloc();
    return std::shared_ptr<uint8_t>(
        reinterpret_cast<uint8_t *>(addr), [](void *addr) { VirtualFree(addr, 0, MEM_RELEASE); });
#else
#pragma error("Aligned alloc not supported")
#endif
}

int main()
{
    try {
        auto vdevice = VDevice::create().expect("Failed create vdevice");
        std::cout << "VDevice created" << std::endl;

        std::vector<std::string> hef_files = {HEF_FILE, HEF_FILE};
        std::map<std::shared_ptr<ConfiguredInferModel>, std::shared_ptr<ConfiguredInferModel::Bindings>>
            configured_infer_models;
        /* The buffers are stored here to ensure memory safety. They will only be freed once
           the configured_infer_model is released, guaranteeing that the buffers remain intact
           until the configured_infer_model is done using them */
        std::vector<std::shared_ptr<uint8_t>> buffer_guards;

        for (const auto &hef_file : hef_files) {
            auto infer_model =
                vdevice->create_infer_model(hef_file).expect("Failed to create infer model from HEF file " + hef_file);
            std::cout << "InferModel created" << std::endl;

            auto configured_infer_model = infer_model->configure().expect("Failed to create configured infer model");
            std::cout << "ConfiguredInferModel created" << std::endl;

            auto bindings = configured_infer_model.create_bindings().expect("Failed to create infer bindings");
            for (const auto &input_name : infer_model->get_input_names()) {
                size_t input_frame_size = infer_model->input(input_name)->get_frame_size();
                auto input_buffer = page_aligned_alloc(input_frame_size);
                auto status = bindings.input(input_name)->set_buffer(MemoryView(input_buffer.get(), input_frame_size));
                if (HAILO_SUCCESS != status) {
                    throw hailort_error(status, "Failed to set infer input buffer");
                }

                buffer_guards.push_back(input_buffer);
            }

            for (const auto &output_name : infer_model->get_output_names()) {
                size_t output_frame_size = infer_model->output(output_name)->get_frame_size();
                auto output_buffer = page_aligned_alloc(output_frame_size);
                auto status =
                    bindings.output(output_name)->set_buffer(MemoryView(output_buffer.get(), output_frame_size));
                if (HAILO_SUCCESS != status) {
                    throw hailort_error(status, "Failed to set infer output buffer");
                }

                buffer_guards.push_back(output_buffer);
            }

            std::cout << "ConfiguredInferModel::Bindings created and configured" << std::endl;

            // Store the configured infer model and its bindings in the map
            configured_infer_models[std::make_shared<ConfiguredInferModel>(configured_infer_model)] =
                std::make_shared<ConfiguredInferModel::Bindings>(bindings);
        }

        std::vector<AsyncInferJob> jobs;
        std::cout << "Running inference" << std::endl;
        for (const auto &configured_infer_model_pair : configured_infer_models) {
            const auto &configured_infer_model = configured_infer_model_pair.first;
            const auto &bindings = *(configured_infer_model_pair.second);
            auto job = configured_infer_model->run_async(bindings).expect("Failed to start async infer job");
            jobs.push_back(std::move(job));
        }

        for (auto &job : jobs) {
            auto status = job.wait(std::chrono::milliseconds(1000));
            if (HAILO_SUCCESS != status) {
                throw hailort_error(status, "Failed to wait for infer to finish");
            }
        }
        std::cout << "Inference finished successfully" << std::endl;
    } catch (const hailort_error &exception) {
        std::cout << "Failed to run inference. status=" << exception.status() << ", error message " << exception.what()
                  << std::endl;
        return -1;
    };

    return 0;
}
