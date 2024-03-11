/**
 * Copyright (c) 2020-2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file async_infer_advanced_example.cpp
 * This example demonstrates the Async Infer API usage with a specific model that has multi-planar input
 * and changes configutrations of the streams.
 * Multiple infer jobs are triggered, and waiting for the last one ensures that all the rest will arrive as well.
 **/

#include "hailo/hailort.hpp"

#include <iostream>

#if defined(__unix__)
#include <sys/mman.h>
#endif

#define FRAMES_COUNT (100)

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
    auto infer_model_exp = vdevice.value()->create_infer_model("hefs/shortcut_nv21.hef");
    if (!infer_model_exp) {
        std::cerr << "Failed to create infer model, status = " << infer_model_exp.status() << std::endl;
        return infer_model_exp.status();
    }
    auto infer_model = infer_model_exp.release();

    infer_model->output()->set_format_type(HAILO_FORMAT_TYPE_FLOAT32);

    // Configure the infer model
    auto configured_infer_model = infer_model->configure();
    if (!configured_infer_model) {
        std::cerr << "Failed to create configured infer model, status = " << configured_infer_model.status() << std::endl;
        return configured_infer_model.status();
    }

    // The buffers are stored here as a guard for the memory. The buffer will be freed only after
    // configured_infer_model will be released.
    std::vector<std::shared_ptr<uint8_t>> buffer_guards;

    // Create infer bindings
    auto bindings = configured_infer_model->create_bindings();
    if (!bindings) {
        std::cerr << "Failed to create infer bindings, status = " << bindings.status() << std::endl;
        return bindings.status();
    }

    // Set the input buffers of the bindings.
    for (const auto &input_name : infer_model->get_input_names()) {
        size_t input_frame_size = infer_model->input(input_name)->get_frame_size();

        // create pix_buffer
        const auto Y_PLANE_SIZE = static_cast<uint32_t>(input_frame_size * 2 / 3);
        const auto VU_PLANE_SIZE = static_cast<uint32_t>(input_frame_size * 1 / 3);
        assert (Y_PLANE_SIZE + VU_PLANE_SIZE == input_frame_size);
        auto y_plane_buffer = page_aligned_alloc(Y_PLANE_SIZE);
        auto vu_plane_buffer = page_aligned_alloc(VU_PLANE_SIZE);
        hailo_pix_buffer_t pix_buffer{};
        pix_buffer.memory_type = HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR;
        pix_buffer.number_of_planes = 2;
        // Y Plane
        pix_buffer.planes[0].bytes_used = Y_PLANE_SIZE;
        pix_buffer.planes[0].plane_size = Y_PLANE_SIZE;
        pix_buffer.planes[0].user_ptr = reinterpret_cast<void*>(y_plane_buffer.get());
        // VU Plane
        pix_buffer.planes[1].bytes_used = VU_PLANE_SIZE;
        pix_buffer.planes[1].plane_size = VU_PLANE_SIZE;
        pix_buffer.planes[1].user_ptr = reinterpret_cast<void*>(vu_plane_buffer.get());

        auto status = bindings->input(input_name)->set_pix_buffer(pix_buffer);
        if (HAILO_SUCCESS != status) {
            std::cerr << "Failed to set infer input buffer, status = " << status << std::endl;
            return status;
        }

        buffer_guards.push_back(y_plane_buffer);
        buffer_guards.push_back(vu_plane_buffer);
    }

    // Set the output buffers of the bindings.
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

    AsyncInferJob last_infer_job;
    for (uint32_t i = 0; i < FRAMES_COUNT; i++) {
        // Waiting for available requests in the pipeline
        auto status = configured_infer_model->wait_for_async_ready(std::chrono::milliseconds(1000));
        if (HAILO_SUCCESS != status) {
            std::cerr << "Failed to wait for async ready, status = " << status << std::endl;
            return status;
        }

        auto job = configured_infer_model->run_async(bindings.value(), [] (const AsyncInferCompletionInfo &/*completion_info*/) {
            // Use completion_info to get the job status and the corresponding bindings
        });
        if (!job) {
            std::cerr << "Failed to start async infer job, status = " << job.status() << std::endl;
            return job.status();
        }
        // detach() is called in order for jobs to run in parallel (and not one after the other)
        job->detach();

        if (i == FRAMES_COUNT - 1) {
            last_infer_job = job.release();
        }
    }

    // Wait for last infer to finish
    auto status = last_infer_job.wait(std::chrono::milliseconds(1000));
    if (HAILO_SUCCESS != status) {
        std::cerr << "Failed to wait for infer to finish, status = " << status << std::endl;
        return status;
    }

    std::cout << "Inference finished successfully" << std::endl;
    return HAILO_SUCCESS;
}
