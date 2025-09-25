/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file raw_streams_example
 * This example demonstrates using low level streams over c++
 **/

#include "hailo/hailort.hpp"

#include <iostream>
#include <thread>


#define HEF_FILE ("hefs/shortcut_net.hef")
constexpr size_t FRAMES_COUNT = 100;
constexpr hailo_format_type_t FORMAT_TYPE = HAILO_FORMAT_TYPE_AUTO;
constexpr size_t MAX_LAYER_EDGES = 16;

using namespace hailort;

Expected<std::shared_ptr<ConfiguredNetworkGroup>> configure_network_group(Device &device)
{
    auto hef = Hef::create(HEF_FILE);
    if (!hef) {
        return make_unexpected(hef.status());
    }

    auto configure_params = device.create_configure_params(hef.value());
    if (!configure_params) {
        return make_unexpected(configure_params.status());
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

void write_all(InputStream &input, hailo_status &status)
{
    auto transform_context = InputTransformContext::create(input.get_info(), {}, FORMAT_TYPE);
    if (!transform_context) {
        status = transform_context.status();
        return;
    }

    std::vector<uint8_t> host_data(transform_context.value()->get_src_frame_size());
    std::vector<uint8_t> hw_data(input.get_frame_size());

    for (size_t i = 0; i < FRAMES_COUNT; i++) {
        status = transform_context.value()->transform(MemoryView(host_data.data(), host_data.size()),
            MemoryView(hw_data.data(), hw_data.size()));
        if (HAILO_SUCCESS != status) {
            return;
        }

        status = input.write(MemoryView(hw_data.data(), hw_data.size()));
        if (HAILO_SUCCESS != status) {
            return ;
        }
    }
    return;
}

void read_all(OutputStream &output, hailo_status &status)
{
    auto transform_context = OutputTransformContext::create(output.get_info(), {}, FORMAT_TYPE);
    if (!transform_context) {
        status = transform_context.status();
        return;
    }

    std::vector<uint8_t> hw_data(output.get_frame_size());
    std::vector<uint8_t> host_data(transform_context.value()->get_dst_frame_size());

    for (size_t i = 0; i < FRAMES_COUNT; i++) {
        status = output.read(MemoryView(hw_data.data(), hw_data.size()));
        if (HAILO_SUCCESS != status) {
            return;
        }

        status = transform_context.value()->transform(MemoryView(hw_data.data(), hw_data.size()),
            MemoryView(host_data.data(), host_data.size()));
        if (HAILO_SUCCESS != status) {
            return;
        }
    }
    return;
}

hailo_status infer(InputStreamRefVector &input_streams, OutputStreamRefVector &output_streams)
{
    hailo_status status = HAILO_SUCCESS; // Success oriented
    hailo_status input_status[MAX_LAYER_EDGES] = {HAILO_UNINITIALIZED};
    hailo_status output_status[MAX_LAYER_EDGES] = {HAILO_UNINITIALIZED};
    std::unique_ptr<std::thread> input_threads[MAX_LAYER_EDGES];
    std::unique_ptr<std::thread> output_threads[MAX_LAYER_EDGES];
    size_t input_thread_index = 0;
    size_t output_thread_index = 0;

    // Create read threads
    for (output_thread_index = 0 ; output_thread_index < output_streams.size(); output_thread_index++) {
        output_threads[output_thread_index] = std::make_unique<std::thread>(read_all,
            output_streams[output_thread_index], std::ref(output_status[output_thread_index]));
    }

    // Create write threads
    for (input_thread_index = 0 ; input_thread_index < input_streams.size(); input_thread_index++) {
        input_threads[input_thread_index] = std::make_unique<std::thread>(write_all, input_streams[input_thread_index],
        std::ref(input_status[input_thread_index]));
    }
    
    // Join write threads
    for (size_t i = 0; i < input_thread_index; i++) {
        input_threads[i]->join();
        if (HAILO_SUCCESS != input_status[i]) {
            status = input_status[i];
        }
    }

    // Join read threads
    for (size_t i = 0; i < output_thread_index; i++) {
        output_threads[i]->join();
        if (HAILO_SUCCESS != output_status[i]) {
            status = output_status[i];
        }
    }

    if (HAILO_SUCCESS == status) {
        std::cout << "Inference finished successfully" << std::endl;
    }

    return status;
}

int main()
{
    auto device = Device::create();
    if (!device) {
        std::cerr << "Failed to create device " << device.status() << std::endl;
        return device.status();
    }

    auto network_group = configure_network_group(*device.value());
    if (!network_group) {
        std::cerr << "Failed to configure network group " << HEF_FILE << std::endl;
        return network_group.status();
    }

    auto inputs = network_group->get()->get_input_streams();
    auto outputs = network_group->get()->get_output_streams();

    auto activated_network_group = network_group.value()->activate();
    if (!activated_network_group) {
        std::cerr << "Failed activated network group "  << activated_network_group.status();
        return activated_network_group.status();
    }

    auto status = infer(inputs, outputs);
    if (HAILO_SUCCESS != status) {
        std::cerr << "Inference failed "  << status << std::endl;
        return status;
    }

    return HAILO_SUCCESS;
}
