/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file raw_async_streams_example
 * This example demonstrates using low level async streams over c++
 **/

#include "hailo/hailort.hpp"
#include "buffer_pool.hpp"

#include <thread>
#include <iostream>


constexpr size_t FRAMES_COUNT = 10000;
constexpr size_t BUFFER_POOL_SIZE = 10;
constexpr auto TIMEOUT = std::chrono::milliseconds(1000);

using namespace hailort;

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

void read_all(OutputStream &output, BufferPoolPtr buffer_pool, size_t frames_to_read, hailo_status &status)
{
    for (size_t i = 0; i < frames_to_read; i++) {
        status = output.wait_for_ready(output.get_frame_size(), TIMEOUT);
        if (HAILO_SUCCESS != status) {
            return;
        }
        status = output.read_async(buffer_pool->dequeue(),
            [buffer_pool](std::shared_ptr<DmaMappedBuffer> buffer, const hailo_async_transfer_completion_info_t &, void *) {
                buffer_pool->enqueue(buffer); 
            });
        if (HAILO_SUCCESS != status) {
            return;
        }
    }
}

void write_all(InputStream &input, BufferPoolPtr buffer_pool, size_t frames_to_write, hailo_status &status)
{
    for (size_t i = 0; i < frames_to_write; i++) {
        status = input.wait_for_ready(input.get_frame_size(), TIMEOUT);
        if (HAILO_SUCCESS != status) {
            return;
        }
        status = input.write_async(buffer_pool->dequeue(),
            [buffer_pool](std::shared_ptr<DmaMappedBuffer> buffer, const hailo_async_transfer_completion_info_t &, void *) {
                buffer_pool->enqueue(buffer); 
            });
        if (HAILO_SUCCESS != status) {
            return;
        }
    }
}

int main()
{
    auto device = Device::create();
    if (!device) {
        std::cerr << "Failed create device " << device.status() << std::endl;
        return device.status();
    }
  
    static const auto HEF_FILE = "hefs/shortcut_net.hef";
    auto network_group = configure_network_group(*device.value(), HEF_FILE);
    if (!network_group) {
        std::cerr << "Failed to configure network group" << HEF_FILE << std::endl;
        return network_group.status();
    }

    auto activated_network_group = network_group.value()->activate();
    if (!activated_network_group) {
        std::cerr << "Failed to activate network group "  << activated_network_group.status() << std::endl;
        return activated_network_group.status();
    }

    // Assume one input and output
    auto output = network_group->get()->get_output_streams()[0];
    auto input = network_group->get()->get_input_streams()[0];

    auto output_buffer_pool = BufferPool::create(BUFFER_POOL_SIZE, output.get().get_frame_size(), HAILO_VDMA_BUFFER_DIRECTION_FLAGS_D2H, *device.value());
    if (!output_buffer_pool) {
        std::cerr << "Failed to create output buffer pool" << std::endl;
        return output_buffer_pool.status();
    }
    hailo_status output_status = HAILO_UNINITIALIZED;
    auto output_thread = std::make_unique<std::thread>(read_all, output, output_buffer_pool.value(), FRAMES_COUNT, std::ref(output_status));
    
    auto input_buffer_pool = BufferPool::create(BUFFER_POOL_SIZE, input.get().get_frame_size(), HAILO_VDMA_BUFFER_DIRECTION_FLAGS_H2D, *device.value());
    if (!input_buffer_pool) {
        std::cerr << "Failed to create input buffer pool" << std::endl;
        return input_buffer_pool.status();
    }
    hailo_status input_status = HAILO_UNINITIALIZED;
    auto input_thread = std::make_unique<std::thread>(write_all, input, input_buffer_pool.value(), FRAMES_COUNT, std::ref(input_status));
    
    // Join threads
    input_thread->join();
    output_thread->join();
    if (HAILO_SUCCESS != input_status) {
        return input_status;
    }
    if (HAILO_SUCCESS != output_status) {
        return output_status;
    }

    // The read/write threads have completed but the transfers issued by them haven't necessarily completed.
    // We'll wait for the output buffer queue to fill back up, since the callback we registered enqueues buffers
    // back to the pool + we issued the same number of reads as writes
    output_buffer_pool.value()->wait_for_pending_buffers();

    return HAILO_SUCCESS;
}
