/**
 * Copyright (c) 2020-2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file async_infer_example.cpp
 * This example demonstrates the Async Infer API usage and assumes the model has only one input and output.
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

    auto infer_model_exp = vdevice.value()->create_infer_model(HEF_FILE);
    if (!infer_model_exp) {
        std::cerr << "Failed to create infer model, status = " << infer_model_exp.status() << std::endl;
        return infer_model_exp.status();
    }
    auto infer_model = infer_model_exp.release();

    auto configured_infer_model = infer_model->configure();
    if (!configured_infer_model) {
        std::cerr << "Failed to create configured infer model, status = " << configured_infer_model.status() << std::endl;
        return configured_infer_model.status();
    }

    auto bindings = configured_infer_model->create_bindings();
    if (!bindings) {
        std::cerr << "Failed to create infer bindings, status = " << bindings.status() << std::endl;
        return bindings.status();
    }

    size_t input_frame_size = infer_model->input()->get_frame_size();
    auto input_buffer = page_aligned_alloc(input_frame_size);
    auto status = bindings->input()->set_buffer(MemoryView(input_buffer.get(), input_frame_size));
    if (HAILO_SUCCESS != status) {
        std::cerr << "Failed to set infer input buffer, status = " << status << std::endl;
        return status;
    }

    size_t output_frame_size = infer_model->output()->get_frame_size();
    auto output_buffer = page_aligned_alloc(output_frame_size);
    status = bindings->output()->set_buffer(MemoryView(output_buffer.get(), output_frame_size));
    if (HAILO_SUCCESS != status) {
        std::cerr << "Failed to set infer input buffer, status = " << status << std::endl;
        return status;
    }

    auto job = configured_infer_model->run_async(bindings.value());
    if (!job) {
        std::cerr << "Failed to start async infer job, status = " << job.status() << std::endl;
        return job.status();
    }

    status = job->wait(std::chrono::milliseconds(1000));
    if (HAILO_SUCCESS != status) {
        std::cerr << "Failed to wait for infer to finish, status = " << status << std::endl;
        return status;
    }
    
    return HAILO_SUCCESS;
}
