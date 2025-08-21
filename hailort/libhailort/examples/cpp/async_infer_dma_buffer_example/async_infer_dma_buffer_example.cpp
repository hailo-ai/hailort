/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file async_infer_dma_buffer_example.cpp
 * This example demonstrates the Async Infer API usage with a specific model using DMA buffers.
 **/

#include "hailo/hailort.hpp"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <iostream>
#include <linux/types.h>

struct dma_heap_allocation_data {
    __u64 len;
    __u32 fd;
    __u32 fd_flags;
    __u64 heap_flags;
};

#define DMA_HEAP_IOC_MAGIC 'H'
#define DMA_HEAP_IOCTL_ALLOC _IOWR(DMA_HEAP_IOC_MAGIC, 0x0, struct dma_heap_allocation_data)

#define HEF_FILE ("hefs/shortcut_net.hef")

using namespace hailort;

class DmaHeapAllocator {
public:
    DmaHeapAllocator(const std::string &path)
    {
        m_heap_fd = open(path.c_str(), O_RDWR);
        if (m_heap_fd < 0) {
            throw hailort_error(HAILO_INTERNAL_FAILURE, "Failed to open dma-heap '" + path + "': " + std::string(strerror(errno)));
        }
    }

    ~DmaHeapAllocator()
    {
        if (m_heap_fd >= 0) { close(m_heap_fd); }
    }

    DmaHeapAllocator(const DmaHeapAllocator&) = delete;
    DmaHeapAllocator& operator=(const DmaHeapAllocator&) = delete;

    hailo_dma_buffer_t allocate(size_t size)
    {
        struct dma_heap_allocation_data data;
        data.len = size;
        data.fd_flags = O_RDWR | O_CLOEXEC;
        data.heap_flags = 0;
        data.fd = 0;

        if (ioctl(m_heap_fd, DMA_HEAP_IOCTL_ALLOC, &data) < 0) {
            int err = errno;
            throw hailort_error(HAILO_INTERNAL_FAILURE, "DMA_HEAP_IOCTL_ALLOC failed: " + std::string(strerror(err)));
        }

        return {(int)data.fd, data.len};
    }

    static void cleanup_dma_buffers(std::vector<hailo_dma_buffer_t> &buffer_guards)
    {
        for (auto &dma_buffer : buffer_guards) {
            ::close(dma_buffer.fd);
        }
    }
private:
    int m_heap_fd = -1;
};

int main(int argc, char **argv)
{
    /* The buffers are stored here to ensure memory safety. They will only be closed once
    the inference is completed or an error occurs. */
    std::vector<hailo_dma_buffer_t> buffer_guards;

    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <dma-heap-path> (e.g. /dev/dma_heap/system)" << std::endl;
        return -1;
    }

    try {
        auto vdevice = VDevice::create().expect("Failed create vdevice");
        std::cout << "VDevice created" << std::endl;

        // Create infer model from HEF file.
        auto infer_model = vdevice->create_infer_model(HEF_FILE).expect("Failed to create infer model");
        std::cout << "InferModel created" << std::endl;

        DmaHeapAllocator allocator(argv[1]);

        // Configure the infer model
        auto configured_infer_model = infer_model->configure().expect("Failed to create configured infer model");
        std::cout << "ConfiguredInferModel created" << std::endl;

        auto bindings = configured_infer_model.create_bindings().expect("Failed to create infer bindings");
        for (const auto &input_name : infer_model->get_input_names()) {
            size_t input_frame_size = infer_model->input(input_name)->get_frame_size();
            auto dma_buffer = allocator.allocate(input_frame_size);
            auto status = bindings.input(input_name)->set_dma_buffer(dma_buffer);
            if (HAILO_SUCCESS != status) {
                throw hailort_error(status, "Failed to set infer input dma buffer");
            }

            buffer_guards.push_back(dma_buffer);
        }

        for (const auto &output_name : infer_model->get_output_names()) {
            size_t output_frame_size = infer_model->output(output_name)->get_frame_size();
            auto dma_buffer = allocator.allocate(output_frame_size);
            auto status = bindings.output(output_name)->set_dma_buffer(dma_buffer);
            if (HAILO_SUCCESS != status) {
                throw hailort_error(status, "Failed to set infer output dma buffer");
            }

            buffer_guards.push_back(dma_buffer);
        }
        std::cout << "ConfiguredInferModel::Bindings created and configured" << std::endl;

        std::cout << "Running inference using DMA buffers..." << std::endl;
        // Run the async infer job
        auto job = configured_infer_model.run_async(bindings).expect("Failed to start async infer job");
        auto status = job.wait(std::chrono::milliseconds(1000));
        if (HAILO_SUCCESS != status) {
            throw hailort_error(status, "Failed to wait for infer to finish");
        }

        DmaHeapAllocator::cleanup_dma_buffers(buffer_guards);
        std::cout << "Inference finished successfully" << std::endl;
    } catch (const hailort_error &exception) {
        std::cout << "Failed to run inference using DMA buffers. status=" << exception.status() << ", error message: " << exception.what() << std::endl;
        DmaHeapAllocator::cleanup_dma_buffers(buffer_guards);
        return -1;
    };

    return 0;
}
