/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

#define BATCH_COUNT (100)
#define BATCH_SIZE (2)

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
        auto vdevice = VDevice::create().expect("Failed create vdevice");
        std::cout << "VDevice created" << std::endl;

        // Create infer model from HEF file.
        auto infer_model = vdevice->create_infer_model("hefs/shortcut_net_nv12.hef").expect("Failed to create infer model");
        std::cout << "InferModel created" << std::endl;

        infer_model->output()->set_format_type(HAILO_FORMAT_TYPE_FLOAT32);
        std::cout << "Set output format_type to float32" << std::endl;
        infer_model->set_batch_size(BATCH_SIZE);
        std::cout << "Set batch_size to " << BATCH_SIZE << std::endl;

        /* Buffers are stored here to ensure memory safety and are only freed after
           the configured_infer_model is released, ensuring they remain intact until the model has finished using them. */
        std::vector<std::shared_ptr<uint8_t>> buffer_guards;

        /* When the same buffers are used multiple times on async-io, to improve performance, it is recommended to pre-map it
           into the VDevice. The DmaMappedBuffer object manages the mapping, and it'll be unmapped when it is destroyed.
           Notice that the buffer must be alive as long as the mapping is alive, so it is defined after 'buffer_guards'. */
        std::vector<DmaMappedBuffer> buffer_map_guards;

        // Configure the infer model
        auto configured_infer_model = infer_model->configure().expect("Failed to create configured infer model");
        std::cout << "ConfiguredInferModel created" << std::endl;

        // Create infer bindings
        auto bindings = configured_infer_model.create_bindings().expect("Failed to create infer bindings");

        // Set the input buffers of the bindings
        for (const auto &input_name : infer_model->get_input_names()) {
            size_t input_frame_size = infer_model->input(input_name)->get_frame_size();

            const auto Y_PLANE_SIZE = static_cast<uint32_t>(input_frame_size * 2 / 3);
            const auto UV_PLANE_SIZE = static_cast<uint32_t>(input_frame_size * 1 / 3);
            assert (Y_PLANE_SIZE + UV_PLANE_SIZE == input_frame_size);

            // Allocate and map Y-plane buffer
            auto y_plane_buffer = page_aligned_alloc(Y_PLANE_SIZE);
            buffer_guards.push_back(y_plane_buffer);
            auto input_mapping_y = DmaMappedBuffer::create(*vdevice, y_plane_buffer.get(), Y_PLANE_SIZE, HAILO_DMA_BUFFER_DIRECTION_H2D).expect("Failed to map input buffer to VDevice");
            buffer_map_guards.push_back(std::move(input_mapping_y));

            // Allocate and map UV-plane buffer
            auto uv_plane_buffer = page_aligned_alloc(UV_PLANE_SIZE);
            buffer_guards.push_back(uv_plane_buffer);
            auto input_mapping_uv = DmaMappedBuffer::create(*vdevice, uv_plane_buffer.get(), Y_PLANE_SIZE, HAILO_DMA_BUFFER_DIRECTION_H2D).expect("Failed to map input buffer to VDevice");
            buffer_map_guards.push_back(std::move(input_mapping_uv));

            // create pix_buffer
            hailo_pix_buffer_t pix_buffer{};
            pix_buffer.memory_type = HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR;
            pix_buffer.number_of_planes = 2;
            // Y Plane
            pix_buffer.planes[0].bytes_used = Y_PLANE_SIZE;
            pix_buffer.planes[0].plane_size = Y_PLANE_SIZE;
            pix_buffer.planes[0].user_ptr = reinterpret_cast<void*>(y_plane_buffer.get());
            // UV Plane
            pix_buffer.planes[1].bytes_used = UV_PLANE_SIZE;
            pix_buffer.planes[1].plane_size = UV_PLANE_SIZE;
            pix_buffer.planes[1].user_ptr = reinterpret_cast<void*>(uv_plane_buffer.get());

            auto status = bindings.input(input_name)->set_pix_buffer(pix_buffer);
            if (HAILO_SUCCESS != status) {
                throw hailort_error(status, "Failed to set infer input buffer");
            }
        }

        // Set the output buffers of the bindings
        for (const auto &output_name : infer_model->get_output_names()) {
            size_t output_frame_size = infer_model->output(output_name)->get_frame_size();
            auto output_buffer = page_aligned_alloc(output_frame_size);
            buffer_guards.push_back(output_buffer);
            auto output_mapping = DmaMappedBuffer::create(*vdevice, output_buffer.get(), output_frame_size, HAILO_DMA_BUFFER_DIRECTION_D2H).expect("Failed to map output buffer to VDevice");
            buffer_map_guards.push_back(std::move(output_mapping));

            auto status = bindings.output(output_name)->set_buffer(MemoryView(output_buffer.get(), output_frame_size));
            if (HAILO_SUCCESS != status) {
                throw hailort_error(status, "Failed to set infer output buffer");
            }
        }
        std::cout << "ConfiguredInferModel::Bindings created and configured" << std::endl;

        // In this example we infer the same buffers, so setting 'BATCH_SIZE' identical bindings in the 'multiple_bindings' vector
        std::vector<ConfiguredInferModel::Bindings> multiple_bindings(BATCH_SIZE, bindings);

        std::cout << "Running inference..." << std::endl;
        AsyncInferJob last_infer_job;
        for (uint32_t i = 0; i < BATCH_COUNT; i++) {
            // Waiting for available requests in the pipeline
            auto status = configured_infer_model.wait_for_async_ready(std::chrono::milliseconds(1000), BATCH_SIZE);
            if (HAILO_SUCCESS != status) {
                throw hailort_error(status, "Failed to wait for async ready");
            }

            auto job = configured_infer_model.run_async(multiple_bindings, [multiple_bindings] (const AsyncInferCompletionInfo &completion_info) {
                // Use completion_info to get the async operation status
                // Note that this callback must be executed as quickly as possible
                (void)completion_info.status;

                // If you want to use the bindings in the callback, capture them by value to make a copy
                // so that they won't be changed in the next infer request
                (void)multiple_bindings;
            }).expect("Failed to start async infer job");

            // detach() is called in order for jobs to run in parallel (and not one after the other)
            job.detach();

            if (i == BATCH_COUNT - 1) {
                last_infer_job = std::move(job);
            }
        }

        // Wait for last infer to finish
        auto status = last_infer_job.wait(std::chrono::milliseconds(1000));
        if (HAILO_SUCCESS != status) {
            throw hailort_error(status, "Failed to wait for infer to finish");
        }

        std::cout << "Inference finished successfully on " << BATCH_COUNT * BATCH_SIZE << " frames" << std::endl;
    } catch (const hailort_error &exception) {
        std::cout << "Failed to run inference. status=" << exception.status() << ", error message: " << exception.what() << std::endl;
        return -1;
    };

    return 0;
}
