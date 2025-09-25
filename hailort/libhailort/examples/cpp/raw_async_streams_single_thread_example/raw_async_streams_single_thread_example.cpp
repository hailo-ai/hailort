/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file raw_async_streams_single_thread_example
 * This example demonstrates using low level async streams using single thread over c++.
 **/

#include "hailo/hailort.hpp"

#include <thread>
#include <iostream>
#include <queue>
#include <condition_variable>

#if defined(__unix__)
#include <sys/mman.h>
#endif

using namespace hailort;

using AlignedBuffer = std::shared_ptr<uint8_t>;
static AlignedBuffer page_aligned_alloc(size_t size)
{
#if defined(__unix__)
    auto addr = mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (MAP_FAILED == addr) throw std::bad_alloc();
    return AlignedBuffer(reinterpret_cast<uint8_t*>(addr), [size](void *addr) { munmap(addr, size); });
#elif defined(_MSC_VER)
    auto addr = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!addr) throw std::bad_alloc();
    return AlignedBuffer(reinterpret_cast<uint8_t*>(addr), [](void *addr){ VirtualFree(addr, 0, MEM_RELEASE); });
#else
#pragma error("Aligned alloc not supported")
#endif
}

static hailo_status infer(ConfiguredNetworkGroup &network_group)
{
    // Assume one input and output
    auto &output = network_group.get_output_streams()[0].get();
    auto &input = network_group.get_input_streams()[0].get();

    auto input_queue_size = input.get_async_max_queue_size();
    auto output_queue_size = output.get_async_max_queue_size();
    if (!input_queue_size || !output_queue_size) {
        std::cerr << "Failed getting async queue size" << std::endl;
        return HAILO_INTERNAL_FAILURE;
    }

    // Allocate buffers. The buffers sent to the async API must be page aligned.
    // Note - the buffers can be freed only after all callbacks are called. The user can either wait for all
    // callbacks, or as done in this example, call ConfiguredNetworkGroup::shutdown that will make sure all callbacks
    // are called.
    std::vector<AlignedBuffer> buffer_guards;

    OutputStream::TransferDoneCallback read_done = [&output, &read_done](const OutputStream::CompletionInfo &completion_info) {
        hailo_status status = HAILO_UNINITIALIZED;
        switch (completion_info.status) {
        case HAILO_SUCCESS:
            // Real applications can forward the buffer to post-process/display. Here we just re-launch new async read.
            status = output.read_async(completion_info.buffer_addr, completion_info.buffer_size, read_done);
            if ((HAILO_SUCCESS != status) && (HAILO_STREAM_ABORT != status)) {
                std::cerr << "Failed read async with status=" << status << std::endl;
            }
            break;
        case HAILO_STREAM_ABORT:
            // Transfer was canceled, finish gracefully.
            break;
        default:
            std::cerr << "Got an unexpected status on callback. status=" << completion_info.status << std::endl;
        }
    };

    InputStream::TransferDoneCallback write_done = [&input, &write_done](const InputStream::CompletionInfo &completion_info) {
        hailo_status status = HAILO_UNINITIALIZED;
        switch (completion_info.status) {
        case HAILO_SUCCESS:
            // Real applications may free the buffer and replace it with new buffer ready to be sent. Here we just
            // re-launch new async write.
            status = input.write_async(completion_info.buffer_addr, completion_info.buffer_size, write_done);
            if ((HAILO_SUCCESS != status) && (HAILO_STREAM_ABORT != status)) {
                std::cerr << "Failed read async with status=" << status << std::endl;
            }
            break;
        case HAILO_STREAM_ABORT:
            // Transfer was canceled, finish gracefully.
            break;
        default:
            std::cerr << "Got an unexpected status on callback. status=" << completion_info.status << std::endl;
        }
    };

    // We launch "*output_queue_size" async read operation. On each async callback, we launch a new async read operation.
    for (size_t i = 0; i < *output_queue_size; i++) {
        // Buffers read from async operation must be page aligned.
        auto buffer = page_aligned_alloc(output.get_frame_size());
        auto status = output.read_async(buffer.get(), output.get_frame_size(), read_done);
        if (HAILO_SUCCESS != status) {
            std::cerr << "read_async failed with status=" << status << std::endl;
            return status;
        }

        buffer_guards.emplace_back(buffer);
    }

    // We launch "*input_queue_size" async write operation. On each async callback, we launch a new async write operation.
    for (size_t i = 0; i < *input_queue_size; i++) {
        // Buffers written to async operation must be page aligned.
        auto buffer = page_aligned_alloc(input.get_frame_size());
        auto status = input.write_async(buffer.get(), input.get_frame_size(), write_done);
        if (HAILO_SUCCESS != status) {
            std::cerr << "write_async failed with status=" << status << std::endl;
            return status;
        }

        buffer_guards.emplace_back(buffer);
    }

    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Calling shutdown on a network group will ensure that all async operations are done. All pending
    // operations will be canceled and their callbacks will be called with status=HAILO_STREAM_ABORT.
    // Only after the shutdown is called, we can safely free the buffers and any variable captured inside the async
    // callback lambda.
    network_group.shutdown();

    return HAILO_SUCCESS;
}


static Expected<std::shared_ptr<ConfiguredNetworkGroup>> configure_network_group(Device &device, const std::string &hef_path)
{
    auto hef = Hef::create(hef_path);
    if (!hef) {
        return make_unexpected(hef.status());
    }

    auto configure_params = device.create_configure_params(hef.value());
    if (!configure_params) {
        return make_unexpected(configure_params.status());
    }

    // change stream_params to operate in async mode
    for (auto &ng_name_params_pair : *configure_params) {
        for (auto &stream_params_name_pair : ng_name_params_pair.second.stream_params_by_name) {
            stream_params_name_pair.second.flags = HAILO_STREAM_FLAGS_ASYNC;
        }
    }

    auto network_groups = device.configure(hef.value(), configure_params.value());
    if (!network_groups) {
        return make_unexpected(network_groups.status());
    }

    if (1 != network_groups->size()) {
        std::cerr << "Invalid amount of network groups" << std::endl;
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    return std::move(network_groups->at(0));
}

int main()
{
    auto device = Device::create();
    if (!device) {
        std::cerr << "Failed to create device " << device.status() << std::endl;
        return EXIT_FAILURE;
    }

    static const auto HEF_FILE = "hefs/shortcut_net.hef";
    auto network_group = configure_network_group(*device.value(), HEF_FILE);
    if (!network_group) {
        std::cerr << "Failed to configure network group" << HEF_FILE << std::endl;
        return EXIT_FAILURE;
    }

    auto activated_network_group = network_group.value()->activate();
    if (!activated_network_group) {
        std::cerr << "Failed to activate network group "  << activated_network_group.status() << std::endl;
        return EXIT_FAILURE;
    }

    // Now start the inference
    auto status = infer(*network_group.value());
    if (HAILO_SUCCESS != status) {
        std::cerr << "Inference failed with " << status << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "Inference finished successfully" << std::endl;
    return EXIT_SUCCESS;
}
