/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file raw_async_streams_multi_thread_example
 * This example demonstrates using low level async streams over c++
 **/

#include "hailo/hailort.hpp"

#include <thread>
#include <iostream>

#if defined(__unix__)
#include <sys/mman.h>
#endif

constexpr auto TIMEOUT = std::chrono::milliseconds(1000);

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

Expected<std::shared_ptr<ConfiguredNetworkGroup>> configure_network_group(Device &device, const std::string &hef_path)
{
    auto hef = Hef::create(hef_path);
    if (!hef) {
        return make_unexpected(hef.status());
    }

    auto configure_params = device.create_configure_params(hef.value());
    if (!configure_params) {
        return make_unexpected(configure_params.status());
    }

    // change stream_params here
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

static void output_async_callback(const OutputStream::CompletionInfo &completion_info)
{
    // Real applications can free the buffer or forward it to post-process/display.
    if ((HAILO_SUCCESS != completion_info.status) && (HAILO_STREAM_ABORT != completion_info.status)) {
        // We will get HAILO_STREAM_ABORT when activated_network_group is destructed.
        std::cerr << "Got an unexpected status on callback. status=" << completion_info.status << std::endl;
    }
}

static void input_async_callback(const InputStream::CompletionInfo &completion_info)
{
    // Real applications can free the buffer or reuse it for next transfer.
    if ((HAILO_SUCCESS != completion_info.status) && (HAILO_STREAM_ABORT  != completion_info.status)) {
        // We will get HAILO_STREAM_ABORT  when activated_network_group is destructed.
        std::cerr << "Got an unexpected status on callback. status=" << completion_info.status << std::endl;
    }
}

static hailo_status infer(Device &device, ConfiguredNetworkGroup &network_group)
{
    // Assume one input and output
    auto &output = network_group.get_output_streams()[0].get();
    auto &input = network_group.get_input_streams()[0].get();

    // Allocate buffers. The buffers sent to the async API must be page aligned.
    // For simplicity, in this example, we pass one buffer for each stream (It may be problematic in output since the
    // buffer will be overridden on each read).
    // Note - the buffers can be freed only after all callbacks are called. The user can either wait for all
    // callbacks, or as done in this example, call ConfiguredNetworkGroup::shutdown that will make sure all callbacks
    // are called.
    auto output_buffer = page_aligned_alloc(output.get_frame_size());
    auto input_buffer = page_aligned_alloc(input.get_frame_size());

    // If the same buffer is used multiple times on async-io, to improve performance, it is recommended to pre-map it
    // into the device. The DmaMappedBuffer object manages the mapping, and it'll be unmapped when it is destroyed.
    // Notice that the buffer must be alive as long as the mapping is alive, so we define it after the buffers.
    auto output_mapping = DmaMappedBuffer::create(device, output_buffer.get(), output.get_frame_size(), HAILO_DMA_BUFFER_DIRECTION_D2H);
    auto input_mapping = DmaMappedBuffer::create(device, input_buffer.get(), input.get_frame_size(), HAILO_DMA_BUFFER_DIRECTION_H2D);
    if (!output_mapping || !input_mapping) {
        std::cerr << "Failed to map buffer with status=" << input_mapping.status() << ", " << output_mapping.status() << std::endl;
        return HAILO_INTERNAL_FAILURE;
    }

    std::atomic<hailo_status> output_status(HAILO_UNINITIALIZED);
    std::thread output_thread([&]() {
        while (true) {
            output_status = output.wait_for_async_ready(output.get_frame_size(), TIMEOUT);
            if (HAILO_SUCCESS != output_status) { return; }

            output_status = output.read_async(output_buffer.get(), output.get_frame_size(), output_async_callback);
            if (HAILO_SUCCESS != output_status) { return; }
        }
    });

    std::atomic<hailo_status> input_status(HAILO_UNINITIALIZED);
    std::thread input_thread([&]() {
        while (true) {
            input_status = input.wait_for_async_ready(input.get_frame_size(), TIMEOUT);
            if (HAILO_SUCCESS != input_status) { return; }

            input_status = input.write_async(input_buffer.get(), input.get_frame_size(), input_async_callback);
            if (HAILO_SUCCESS != input_status) { return; }
        }
    });

    // After all async operations are launched, the inference is running.
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Calling shutdown on a network group will ensure that all async operations are done. All pending
    // operations will be canceled and their callbacks will be called with status=HAILO_STREAM_ABORT.
    // Only after the shutdown is called, we can safely free the buffers and any variable captured inside the async
    // callback lambda.
    network_group.shutdown();

    // Thread should be stopped with HAILO_STREAM_ABORT status.
    output_thread.join();
    input_thread.join();

    if ((HAILO_STREAM_ABORT != output_status) || (HAILO_STREAM_ABORT != input_status)) {
        std::cerr << "Got unexpected statues from thread: " << output_status << ", " << input_status << std::endl;
        return HAILO_INTERNAL_FAILURE;
    }

    return HAILO_SUCCESS;
}

int main()
{
    auto device = Device::create();
    if (!device) {
        std::cerr << "Failed create device " << device.status() << std::endl;
        return EXIT_FAILURE;
    }

    static const auto HEF_FILE = "hefs/shortcut_net.hef";
    auto network_group = configure_network_group(*device.value(), HEF_FILE);
    if (!network_group) {
        std::cerr << "Failed to configure network group " << HEF_FILE << std::endl;
        return EXIT_FAILURE;
    }

    auto activated_network_group = network_group.value()->activate();
    if (!activated_network_group) {
        std::cerr << "Failed to activate network group "  << activated_network_group.status() << std::endl;
        return EXIT_FAILURE;
    }

    auto status = infer(*device.value(), *network_group.value());
    if (HAILO_SUCCESS != status) {
        return EXIT_FAILURE;
    }

    std::cout << "Inference finished successfully" << std::endl;
    return EXIT_SUCCESS;
}