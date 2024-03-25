/**
 * Copyright (c) 2020-2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file async_infer_basic_example.cpp
 * This example demonstrates the Async Infer API usage with a specific model.
 **/

#include "hailo/hailort.hpp"

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
    auto vdevice = VDevice::create();
    if (!vdevice) {
        std::cerr << "Failed create vdevice, status = " << vdevice.status() << std::endl;
        return vdevice.status();
    }

    // Create infer model from HEF file.
    auto infer_model_exp = vdevice.value()->create_infer_model(HEF_FILE);
    if (!infer_model_exp) {
        std::cerr << "Failed to create infer model, status = " << infer_model_exp.status() << std::endl;
        return infer_model_exp.status();
    }
    auto infer_model = infer_model_exp.release();

    // Configure the infer model
    auto configured_infer_model = infer_model->configure();
    if (!configured_infer_model) {
        std::cerr << "Failed to create configured infer model, status = " << configured_infer_model.status() << std::endl;
        return configured_infer_model.status();
    }

    // The buffers are stored here as a guard for the memory. The buffer will be freed only after
    // configured_infer_model will be released.
    std::vector<std::shared_ptr<uint8_t>> buffer_guards;

    auto bindings = configured_infer_model->create_bindings();
    if (!bindings) {
        std::cerr << "Failed to create infer bindings, status = " << bindings.status() << std::endl;
        return bindings.status();
    }

    for (const auto &input_name : infer_model->get_input_names()) {
        size_t input_frame_size = infer_model->input(input_name)->get_frame_size();
        auto input_buffer = page_aligned_alloc(input_frame_size);
        auto status = bindings->input(input_name)->set_buffer(MemoryView(input_buffer.get(), input_frame_size));
        if (HAILO_SUCCESS != status) {
            std::cerr << "Failed to set infer input buffer, status = " << status << std::endl;
            return status;
        }

        buffer_guards.push_back(input_buffer);
    }

    for (const auto &output_name : infer_model->get_output_names()) {
        size_t output_frame_size = infer_model->output(output_name)->get_frame_size();
        auto output_buffer = page_aligned_alloc(output_frame_size);
        auto status = bindings->output(output_name)->set_buffer(MemoryView(output_buffer.get(), output_frame_size));
        if (HAILO_SUCCESS != status) {
            std::cerr << "Failed to set infer output buffer, status = " << status << std::endl;
            return status;
        }

        buffer_guards.push_back(output_buffer);
    }

    // Run the async infer job.
    auto job = configured_infer_model->run_async(bindings.value());
    if (!job) {
        std::cerr << "Failed to start async infer job, status = " << job.status() << std::endl;
        return job.status();
    }

    auto status = job->wait(std::chrono::milliseconds(1000));
    if (HAILO_SUCCESS != status) {
        std::cerr << "Failed to wait for infer to finish, status = " << status << std::endl;
        return status;
    }

    std::cout << "Inference finished successfully" << std::endl;
    return HAILO_SUCCESS;
}
