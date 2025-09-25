/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file multi_network_vstream_example.cpp
 * This example demonstrates multi network with virtual streams over c++
 **/

#include "hailo/hailort.hpp"

#include <iostream>
#include <thread>


#define HEF_FILE ("hefs/multi_network_shortcut_net.hef")
constexpr size_t INFER_FRAME_COUNT = 100;
constexpr hailo_format_type_t FORMAT_TYPE = HAILO_FORMAT_TYPE_AUTO;
constexpr size_t MAX_LAYER_EDGES = 16;
constexpr size_t NET_GROUPS_COUNT = 1;
constexpr size_t NET_COUNT = 2;
constexpr size_t FIRST_NET_BATCH_SIZE = 1;
constexpr size_t SECOND_NET_BATCH_SIZE = 2;
constexpr uint32_t DEVICE_COUNT = 1;

using namespace hailort;
using InOutVStreams = std::pair<std::vector<InputVStream>, std::vector<OutputVStream>>;

Expected<std::shared_ptr<ConfiguredNetworkGroup>> configure_network_group(VDevice &vdevice, Hef &hef, uint16_t batch_size[NET_COUNT])
{
    auto configure_params = vdevice.create_configure_params(hef);
    if (!configure_params) {
        std::cerr << "Failed to create configure params" << std::endl;
        return make_unexpected(configure_params.status());
    }
    if (NET_GROUPS_COUNT != configure_params->size()) {
        std::cerr << "Invalid amount of network groups configure params" << std::endl;
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    // Modify batch_size for each network
    size_t net_index = 0;
    for (auto &net_name_params_pair : configure_params->begin()->second.network_params_by_name) {
        net_name_params_pair.second.batch_size = batch_size[net_index];
        net_index++;
    }

    auto network_groups = vdevice.configure(hef, configure_params.value());
    if (!network_groups) {
        std::cerr << "Failed to configure vdevice" << std::endl;
        return make_unexpected(network_groups.status());
    }

    if (NET_GROUPS_COUNT != network_groups->size()) {
        std::cerr << "Invalid amount of network groups" << std::endl;
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    return std::move(network_groups->at(0));
}

Expected<std::vector<hailo_network_info_t>> get_network_infos(ConfiguredNetworkGroup &net_group)
{
    auto networks_infos = net_group.get_network_infos();
    if (!networks_infos) {
        std::cerr << "Failed to get networks infos of network group " << net_group.name() << std::endl;
        return make_unexpected(networks_infos.status());
    }
    if (NET_COUNT != networks_infos->size()) {
        std::cerr << "Invalid amount of networks in group " << net_group.name() << std::endl;
        return make_unexpected(networks_infos.status());
    }

    return networks_infos.release();
}

Expected<std::map<std::string, InOutVStreams>> create_vstreams_per_network(ConfiguredNetworkGroup &net_group,
    std::vector<hailo_network_info_t> &networks_infos)
{
    // Create vstreams for each network
    std::map<std::string, InOutVStreams> networks_vstreams;
    for (auto &network_info : networks_infos) {
        auto vstreams = VStreamsBuilder::create_vstreams(net_group, {}, FORMAT_TYPE, network_info.name);
        if (!vstreams) {
            std::cerr << "Failed to create vstreams for network " << network_info.name << std::endl;
            return make_unexpected(vstreams.status());
        }

        if (vstreams->first.size() > MAX_LAYER_EDGES || vstreams->second.size() > MAX_LAYER_EDGES) {
            std::cerr << "Trying to infer network with too many input/output virtual streams, Maximum amount is " <<
            MAX_LAYER_EDGES << " (either change HEF or change the definition of MAX_LAYER_EDGES)"<< std::endl;
            return make_unexpected(HAILO_INVALID_OPERATION);
        }

        networks_vstreams.emplace(network_info.name, vstreams.release());
    }

    return networks_vstreams;
}

void write_all(InputVStream &input, hailo_status &status, uint16_t batch_size)
{
    std::vector<uint8_t> data(input.get_frame_size());
    const size_t network_frames_count = INFER_FRAME_COUNT * batch_size;
    for (size_t i = 0; i < network_frames_count; i++) {
        status = input.write(MemoryView(data.data(), data.size()));
        if (HAILO_SUCCESS != status) {
            return;
        }
    }
    status = HAILO_SUCCESS;
    return;
}

void read_all(OutputVStream &output, hailo_status &status, uint16_t batch_size)
{
    std::vector<uint8_t> data(output.get_frame_size());
    const size_t network_frames_count = INFER_FRAME_COUNT * batch_size;
    for (size_t i = 0; i < network_frames_count; i++) {
        status = output.read(MemoryView(data.data(), data.size()));
        if (HAILO_SUCCESS != status) {
            return;
        }
    }
    status = HAILO_SUCCESS;
    return;
}

hailo_status infer(std::map<std::string, InOutVStreams> &network_vstreams_pairs, uint16_t batch_size[NET_COUNT])
{
    hailo_status status = HAILO_SUCCESS; // Success oriented
    hailo_status input_status[NET_COUNT][MAX_LAYER_EDGES];
    hailo_status output_status[NET_COUNT][MAX_LAYER_EDGES];
    std::unique_ptr<std::thread> input_threads[NET_COUNT][MAX_LAYER_EDGES];
    std::unique_ptr<std::thread> output_threads[NET_COUNT][MAX_LAYER_EDGES];
    size_t input_threads_count[NET_COUNT] = {0};
    size_t output_threads_count[NET_COUNT] = {0};

    size_t net_index = 0;
    for (auto &network_vstream_pair : network_vstreams_pairs) {
        auto &input_vstreams = network_vstream_pair.second.first;
        auto &output_vstreams = network_vstream_pair.second.second;

        // Create read threads
        for (size_t i = 0 ; i < output_vstreams.size(); i++) {
            output_threads[net_index][i] = std::make_unique<std::thread>(read_all,
                std::ref(output_vstreams[i]), std::ref(output_status[net_index][i]), batch_size[net_index]);
        }

        // Create write threads
        for (size_t i = 0 ; i < input_vstreams.size(); i++) {
            input_threads[net_index][i] = std::make_unique<std::thread>(write_all,
                std::ref(input_vstreams[i]), std::ref(input_status[net_index][i]), batch_size[net_index]);
        }

        input_threads_count[net_index] = input_vstreams.size();
        output_threads_count[net_index] = output_vstreams.size();
        net_index++;
    }

    // Join write threads
    for (net_index = 0; net_index < NET_COUNT; net_index++) {
        for (size_t thread_index = 0; thread_index < input_threads_count[net_index]; thread_index++) {
            input_threads[net_index][thread_index]->join();
            if (HAILO_SUCCESS != input_status[net_index][thread_index]) {
                status = input_status[net_index][thread_index];
            }
        }
    }
    // Join read threads
    for (net_index = 0; net_index < NET_COUNT; net_index++) {
        for (size_t thread_index = 0; thread_index < output_threads_count[net_index]; thread_index++) {
            output_threads[net_index][thread_index]->join();
            if (HAILO_SUCCESS != output_status[net_index][thread_index]) {
                status = output_status[net_index][thread_index];
            }
        }
    }

    if (HAILO_SUCCESS == status) {
        std::cout << "Inference finished successfully" << std::endl;
    }

    return status;
}

int main()
{
    uint16_t batch_size[NET_COUNT] = {FIRST_NET_BATCH_SIZE, SECOND_NET_BATCH_SIZE};

    hailo_vdevice_params_t params;
    auto status = hailo_init_vdevice_params(&params);
    if (HAILO_SUCCESS != status) {
        std::cerr << "Failed init vdevice_params, status = " << status << std::endl;
        return status;
    }

    /* Scheduler does not support different batches for different networks within the same network group */
    params.scheduling_algorithm = HAILO_SCHEDULING_ALGORITHM_NONE;
    params.device_count = DEVICE_COUNT;
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
        std::cerr << "Failed to configure network group, status = " << network_group.status() << std::endl;
        return network_group.status();
    }

    auto network_infos = get_network_infos(*network_group.value());
    if (!network_infos) {
        std::cerr << "Failed to get network infos, status = " << network_infos.status() << std::endl;
        return network_infos.status();
    }

    auto vstreams = create_vstreams_per_network(*network_group.value(), network_infos.value());
    if (!vstreams) {
        std::cerr << "Failed creating vstreams, status = " << vstreams.status() << std::endl;
        return vstreams.status();
    }

    auto activated_network_group = network_group.value()->activate();
    if (!activated_network_group) {
        std::cerr << "Failed activated network group, status = "  << activated_network_group.status();
        return activated_network_group.status();
    }

    status = infer(vstreams.value(), batch_size);
    if (HAILO_SUCCESS != status) {
        std::cerr << "Inference failed, status = "  << status << std::endl;
        return status;
    }

    return HAILO_SUCCESS;
}
