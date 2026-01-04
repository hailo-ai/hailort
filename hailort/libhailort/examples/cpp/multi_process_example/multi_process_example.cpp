/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file multi_process_example.cpp
 * This example is used to demonstrate the multi-process inference functionality on Hailo-10H devices.
 * Multi-process inference is supported by default on Hailo-10H devices, thus there is no need to enable the multi-process flag.
 **/

#include "hailo/hailort.hpp"

#include <iostream>

#if defined(__unix__)
#include <sys/mman.h>
#endif

#define HEF_FILE ("hefs/shortcut_net.hef")
#define FRAMES_COUNT (100)
#define WAIT_TIMEOUT_MS (10000)

using namespace hailort;

static std::shared_ptr<uint8_t> page_aligned_alloc(size_t size)
{
#if defined(__unix__)
    auto addr = mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (MAP_FAILED == addr) throw std::bad_alloc();
    return std::shared_ptr<uint8_t>(reinterpret_cast<uint8_t*>(addr), [size](void *addr) { munmap(addr, size); });
#elif defined(_MSC_VER)
    auto addr = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!addr) throw std::bad_alloc();
    return std::shared_ptr<uint8_t>(reinterpret_cast<uint8_t*>(addr), [](void *addr){ VirtualFree(addr, 0, MEM_RELEASE); });
#else
#pragma error("Aligned alloc not supported")
#endif
}

int main()
{
    try {
        hailo_vdevice_params_t params;
        auto status = hailo_init_vdevice_params(&params);
        if (HAILO_SUCCESS != status) {
            throw hailort_error(status, "Failed to initialize vdevice params");
        }

        // No need to set the multi_process_service flag, this functionality is supported by default on Hailo-10H devices.
        params.group_id = "SHARED";

        auto vdevice = VDevice::create(params).expect("Failed to create vdevice");
        std::cout << "VDevice created" << std::endl;

        // Create infer model from HEF file.
        auto infer_model = vdevice->create_infer_model(HEF_FILE).expect("Failed to create infer model");
        std::cout << "InferModel created" << std::endl;

        /* The buffers are stored here to ensure memory safety. They will only be freed once
           the configured_infer_model is released, guaranteeing that the buffers remain intact 
           until the configured_infer_model is done using them */
        std::vector<std::shared_ptr<uint8_t>> buffer_guards;

        // Configure the infer model
        auto configured_infer_model = infer_model->configure().expect("Failed to create configured infer model");
        std::cout << "ConfiguredInferModel created" << std::endl;

        auto bindings = configured_infer_model.create_bindings().expect("Failed to create infer bindings");
        for (const auto &input_name : infer_model->get_input_names()) {
            size_t input_frame_size = infer_model->input(input_name)->get_frame_size();
            auto input_buffer = page_aligned_alloc(input_frame_size);
            status = bindings.input(input_name)->set_buffer(MemoryView(input_buffer.get(), input_frame_size));
            if (HAILO_SUCCESS != status) {
                throw hailort_error(status, "Failed to set infer input buffer");
            }

            buffer_guards.push_back(input_buffer);
        }

        for (const auto &output_name : infer_model->get_output_names()) {
            size_t output_frame_size = infer_model->output(output_name)->get_frame_size();
            auto output_buffer = page_aligned_alloc(output_frame_size);
            status = bindings.output(output_name)->set_buffer(MemoryView(output_buffer.get(), output_frame_size));
            if (HAILO_SUCCESS != status) {
                throw hailort_error(status, "Failed to set infer output buffer");
            }

            buffer_guards.push_back(output_buffer);
        }
        std::cout << "ConfiguredInferModel::Bindings created and configured" << std::endl;

        std::cout << "Running inference..." << std::endl;
        AsyncInferJob last_infer_job;
        for (uint32_t i = 0; i < FRAMES_COUNT; i++) {
            status = configured_infer_model.wait_for_async_ready(std::chrono::milliseconds(WAIT_TIMEOUT_MS));
            if (HAILO_SUCCESS != status) {
                throw hailort_error(status, "Failed to wait for async ready");
            }

            auto job = configured_infer_model.run_async(bindings).expect("Failed to start async infer job");

            // detach() is called in order for jobs to run in parallel (and not one after the other)
            job.detach();

            if (i == FRAMES_COUNT - 1) {
                last_infer_job = std::move(job);
            }
        }

        // Wait for last infer to finish
        status = last_infer_job.wait(std::chrono::milliseconds(WAIT_TIMEOUT_MS));
        if (HAILO_SUCCESS != status) {
            throw hailort_error(status, "Failed to wait for infer to finish");
        }

        std::cout << "Inference finished successfully" << std::endl;
    } catch (const hailort_error &exception) {
        std::cout << "Failed to run inference. status=" << exception.status() << ", error message: " << exception.what() << std::endl;
        return -1;
    };

    return 0;
}
