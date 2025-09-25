/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file multi_device_example.cpp
 * This example demonstrates how to work with multiple devices using virtual device.
 **/

#include "hailo/hailort.hpp"

#include <iostream>
#include <thread>


#define HEF_FILE ("hefs/shortcut_net.hef")
constexpr size_t BATCH_SIZE = 1;
constexpr size_t FRAMES_COUNT = 100;
constexpr hailo_format_type_t FORMAT_TYPE = HAILO_FORMAT_TYPE_AUTO;
constexpr size_t MAX_LAYER_EDGES = 16;

using namespace hailort;

Expected<std::shared_ptr<ConfiguredNetworkGroup>> configure_network_group(VDevice &vdevice, Hef &hef, uint16_t batch_size)
{
    auto configure_params = vdevice.create_configure_params(hef);
    if (!configure_params) {
        std::cerr << "Failed to create configure params" << std::endl;
        return make_unexpected(configure_params.status());
    }

    // Modify batch_size and power_mode for each network group
    for (auto& network_group_params : configure_params.value()) {
        network_group_params.second.batch_size = batch_size;
        network_group_params.second.power_mode = HAILO_POWER_MODE_ULTRA_PERFORMANCE;
    }

    auto network_groups = vdevice.configure(hef, configure_params.value());
    if (!network_groups) {
        std::cerr << "Failed to configure vdevice" << std::endl;
        return make_unexpected(network_groups.status());
    }

    if (1 != network_groups->size()) {
        std::cerr << "Invalid amount of network groups" << std::endl;
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    return std::move(network_groups->at(0));
}

void write_all(InputVStream &input, hailo_status &status)
{
    std::vector<uint8_t> data(input.get_frame_size());
    for (size_t i = 0; i < FRAMES_COUNT; i++) {
        status = input.write(MemoryView(data.data(), data.size()));
        if (HAILO_SUCCESS != status) {
            return;
        }
    }

    // Flushing is not mandatory here
    status = input.flush();
    if (HAILO_SUCCESS != status) {
        std::cerr << "Failed flushing input vstream" << std::endl;
        return;
    }

    status = HAILO_SUCCESS;
    return;
}

void read_all(OutputVStream &output, hailo_status &status)
{
    std::vector<uint8_t> data(output.get_frame_size());
    for (size_t i = 0; i < FRAMES_COUNT; i++) {
        status = output.read(MemoryView(data.data(), data.size()));
        if (HAILO_SUCCESS != status) {
            return;
        }
    }
    status = HAILO_SUCCESS;
    return;
}

hailo_status infer(std::vector<InputVStream> &input_streams, std::vector<OutputVStream> &output_streams)
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
            std::ref(output_streams[output_thread_index]), std::ref(output_status[output_thread_index]));
    }

    // Create write threads
    for (input_thread_index = 0 ; input_thread_index < input_streams.size(); input_thread_index++) {
        input_threads[input_thread_index] = std::make_unique<std::thread>(write_all,
            std::ref(input_streams[input_thread_index]), std::ref(input_status[input_thread_index]));
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
    uint16_t batch_size = BATCH_SIZE;

    auto scan_res = hailort::Device::scan();
    if (!scan_res) {
        std::cerr << "Failed to scan, status = " << scan_res.status() << std::endl;
        return scan_res.status();
    }
    std::cout << "Found " << scan_res.value().size() << " devices" << std::endl;

    hailo_vdevice_params_t params;
    auto status = hailo_init_vdevice_params(&params);
    if (HAILO_SUCCESS != status) {
        std::cerr << "Failed init vdevice_params, status = " << status << std::endl;
        return status;
    }

    params.device_count = static_cast<uint32_t>(scan_res->size());
    auto vdevice = VDevice::create(params);
    if (!vdevice) {
        std::cerr << "Failed create vdevice, status = " << vdevice.status() << std::endl;
        return vdevice.status();
    }

    auto hef = Hef::create(HEF_FILE);
    if (!hef) {
        std::cerr << "Failed to create hef: " << HEF_FILE  << ", status = " << hef.status() << std::endl;
        return hef.status();
    }

    auto network_group = configure_network_group(*vdevice.value(), hef.value(), batch_size);
    if (!network_group) {
        std::cerr << "Failed to configure network group " << HEF_FILE << std::endl;
        return network_group.status();
    }

    auto vstreams = VStreamsBuilder::create_vstreams(*network_group.value(), {}, FORMAT_TYPE);
    if (!vstreams) {
        std::cerr << "Failed creating vstreams " << vstreams.status() << std::endl;
        return vstreams.status();
    }

    if (vstreams->first.size() > MAX_LAYER_EDGES || vstreams->second.size() > MAX_LAYER_EDGES) {
        std::cerr << "Trying to infer network with too many input/output virtual streams, Maximum amount is " <<
        MAX_LAYER_EDGES << " (either change HEF or change the definition of MAX_LAYER_EDGES)"<< std::endl;
        return HAILO_INVALID_OPERATION;
    }

    status = infer(vstreams->first, vstreams->second);
    if (HAILO_SUCCESS != status) {
        std::cerr << "Inference failed "  << status << std::endl;
        return status;
    }

    return HAILO_SUCCESS;
}
