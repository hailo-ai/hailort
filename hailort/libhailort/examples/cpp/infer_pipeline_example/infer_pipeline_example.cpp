/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file infer_pipeline_example.cpp
 * This example demonstrates the basic data-path on HailoRT using the high level API - Virtual Stream Pipeline.
 * The program creates a device according to the provided IP address, generates a random dataset,
 * and runs it through the device with virtual streams pipeline.
 **/

#include "hailo/hailort.hpp"

#include <iostream>


#define HEF_FILE ("hefs/shortcut_net.hef")
constexpr size_t FRAMES_COUNT = 100;
constexpr hailo_format_type_t FORMAT_TYPE = HAILO_FORMAT_TYPE_AUTO;

#define USAGE_ERROR_MSG ("Args parsing error.\nUsage: infer_pipeline_example <ip_address>\n")

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

hailo_status infer(InferVStreams &pipeline)
{
    const size_t frames_count = FRAMES_COUNT;

    auto input_vstreams = pipeline.get_input_vstreams();
    std::map<std::string, std::vector<uint8_t>> input_data;
    for (const auto &input_vstream : input_vstreams) {
        input_data.emplace(input_vstream.get().name(), std::vector<uint8_t>(input_vstream.get().get_frame_size() * frames_count));
    }

    std::map<std::string, MemoryView> input_data_mem_views;
    for (const auto &input_vstream : input_vstreams) {
        auto &input_buffer = input_data[input_vstream.get().name()];
        input_data_mem_views.emplace(input_vstream.get().name(), MemoryView(input_buffer.data(), input_buffer.size()));
    }

    auto output_vstreams = pipeline.get_output_vstreams();
    std::map<std::string, std::vector<uint8_t>> output_data;
    for (const auto &output_vstream : output_vstreams) {
        output_data.emplace(output_vstream.get().name(), std::vector<uint8_t>(output_vstream.get().get_frame_size() * frames_count));
    }

    std::map<std::string, MemoryView> output_data_mem_views;
    for (const auto &output_vstream : output_vstreams) {
        auto &output_buffer = output_data[output_vstream.get().name()];
        output_data_mem_views.emplace(output_vstream.get().name(), MemoryView(output_buffer.data(), output_buffer.size()));
    }

    hailo_status status = pipeline.infer(input_data_mem_views, output_data_mem_views, frames_count);

    return status;
}

Expected<std::string> parse_arguments(int argc, char **argv)
{
    if (2 != argc) {
        std::cerr << USAGE_ERROR_MSG << std::endl;
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }
    return std::string(argv[1]);
}

int main(int argc, char **argv)
{
    auto device_ip = parse_arguments(argc, argv);
    if (!device_ip) {
        std::cerr << "Failed parsing arguments " << device_ip.status() << std::endl;
        return device_ip.status();
    }

    auto device = Device::create_eth(device_ip.value());
    if (!device) {
        std::cerr << "Failed create_eth " << device.status() << std::endl;
        return device.status();
    }

    auto network_group = configure_network_group(*device.value());
    if (!network_group) {
        std::cerr << "Failed to configure network group " << HEF_FILE << std::endl;
        return network_group.status();
    }

    auto input_params = network_group.value()->make_input_vstream_params({}, FORMAT_TYPE, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE);
    if (!input_params) {
        std::cerr << "Failed make_input_vstream_params " << input_params.status() << std::endl;
        return input_params.status();
    }

    auto output_params = network_group.value()->make_output_vstream_params({}, FORMAT_TYPE, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE);
    if (!output_params) {
        std::cerr << "Failed make_output_vstream_params " << output_params.status() << std::endl;
        return output_params.status();
    }

    auto activated_network_group = network_group.value()->activate();
    if (!activated_network_group) {
        std::cerr << "Failed activated network group "  << activated_network_group.status();
        return activated_network_group.status();
    }

    auto pipeline = InferVStreams::create(*network_group.value(), input_params.value(), output_params.value());
    if (!pipeline) {
        std::cerr << "Failed to create inference pipeline " << pipeline.status() << std::endl;
        return pipeline.status();
    }

    auto status = infer(pipeline.value());
    if (HAILO_SUCCESS == status) {
        std::cout << "Inference finished successfully" << std::endl;
    }
    
    return status;
}
