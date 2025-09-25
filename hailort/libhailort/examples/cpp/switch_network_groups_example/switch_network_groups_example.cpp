/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file switch_network_groups_example.cpp
 * This example demonstrates basic usage of HailoRT streaming api over multiple network groups, using VStreams.
 * It loads several network_groups (via several HEFs) into a Hailo VDevice and performs a inferences on all of them in parallel.
 * The network_groups switching is performed automatically by the HailoRT scheduler.
 **/

#include "hailo/hailort.hpp"

#include <iostream>
#include <chrono>
#include <thread>


constexpr hailo_format_type_t FORMAT_TYPE = HAILO_FORMAT_TYPE_AUTO;
constexpr size_t INFER_FRAME_COUNT = 100;
constexpr uint32_t DEVICE_COUNT = 1;
constexpr size_t BATCH_SIZE_1 = 1;
constexpr size_t BATCH_SIZE_2 = 2;

constexpr std::chrono::milliseconds SCHEDULER_TIMEOUT_MS(100);
constexpr uint32_t SCHEDULER_THRESHOLD = 3;

using namespace hailort;
using ThreadsVector = std::vector<std::unique_ptr<std::thread>>;
using StatusVector = std::vector<std::shared_ptr<hailo_status>>;

void write_all(InputVStream &input_vstream, std::shared_ptr<hailo_status> status_out)
{
    std::vector<uint8_t> buff(input_vstream.get_frame_size());

    for (size_t i = 0; i < INFER_FRAME_COUNT; i++) {
        auto status = input_vstream.write(MemoryView(buff.data(), buff.size()));
        if (HAILO_SUCCESS != status) {
            *status_out = status;
            return;
        }
    }
    *status_out = HAILO_SUCCESS;
    return;
}

void read_all(OutputVStream &output_vstream, std::shared_ptr<hailo_status> status_out)
{
    std::vector<uint8_t> buff(output_vstream.get_frame_size());

    for (size_t i = 0; i < INFER_FRAME_COUNT; i++) {
        auto status = output_vstream.read(MemoryView(buff.data(), buff.size()));
        if (HAILO_SUCCESS != status) {
            *status_out = status;
            return;
        }
    }
    *status_out = HAILO_SUCCESS;
    return;
}

Expected<std::vector<std::pair<std::vector<InputVStream>, std::vector<OutputVStream>>>> build_vstreams(
    const std::vector<std::shared_ptr<ConfiguredNetworkGroup>> &configured_network_groups)
{
    std::vector<std::pair<std::vector<InputVStream>, std::vector<OutputVStream>>> vstreams_per_network_group;

    for (auto &network_group : configured_network_groups) {
        auto vstreams_exp = VStreamsBuilder::create_vstreams(*network_group, {}, FORMAT_TYPE);
        if (!vstreams_exp) {
            return make_unexpected(vstreams_exp.status());
        }
        vstreams_per_network_group.emplace_back(vstreams_exp.release());
    }
    return vstreams_per_network_group;
}

void create_read_threads(std::vector<OutputVStream> &vstreams, StatusVector &read_results, ThreadsVector &threads_vector)
{
    for (auto &vstream : vstreams) {
        read_results.push_back(std::make_shared<hailo_status>(HAILO_UNINITIALIZED));
        threads_vector.emplace_back(std::make_unique<std::thread>(read_all, std::ref(vstream), read_results.back()));
    }
}

void create_write_threads(std::vector<InputVStream> &vstreams, StatusVector &write_results, ThreadsVector &threads_vector)
{
    for (auto &vstream : vstreams) {
        write_results.push_back(std::make_shared<hailo_status>(HAILO_UNINITIALIZED));
        threads_vector.emplace_back(std::make_unique<std::thread>(write_all, std::ref(vstream), write_results.back()));
    }
}

Expected<std::unique_ptr<VDevice>> create_vdevice()
{
    hailo_vdevice_params_t params;
    auto status = hailo_init_vdevice_params(&params);
    if (HAILO_SUCCESS != status) {
        std::cerr << "Failed init vdevice_params, status = " << status << std::endl;
        return make_unexpected(status);
    }
    params.device_count = DEVICE_COUNT;

    return VDevice::create(params);
}

Expected<std::vector<std::shared_ptr<ConfiguredNetworkGroup>>> configure_hefs(VDevice &vdevice, std::vector<std::string> &hef_paths,
    const std::vector<uint16_t> &batch_sizes)
{
    std::vector<std::shared_ptr<ConfiguredNetworkGroup>> results;
    assert(hef_paths.size() == batch_sizes.size());

    size_t i = 0;
    for (const auto &path : hef_paths) {
        auto hef_exp = Hef::create(path);
        if (!hef_exp) {
            return make_unexpected(hef_exp.status());
        }
        auto hef = hef_exp.release();

        auto configure_params = vdevice.create_configure_params(hef);
        if (!configure_params) {
            std::cerr << "Failed to create configure params" << std::endl;
            return make_unexpected(configure_params.status());
        }

        // Modify batch_size for each network group
        for (auto& network_group_params : configure_params.value()) {
            network_group_params.second.batch_size = batch_sizes[i];
            network_group_params.second.power_mode = HAILO_POWER_MODE_ULTRA_PERFORMANCE;
        }
        i++;

        auto added_network_groups = vdevice.configure(hef);
        if (!added_network_groups) {
            return make_unexpected(added_network_groups.status());
        }
        results.insert(results.end(), added_network_groups->begin(),
            added_network_groups->end());
    }
    return results;
}

int main()
{
    auto vdevice_exp = create_vdevice();
    if (!vdevice_exp) {
        std::cerr << "Failed create vdevice, status = " << vdevice_exp.status() << std::endl;
        return vdevice_exp.status();
    }
    auto vdevice = vdevice_exp.release();

    // Note: default batch_size is 0, which is not used in this example
    std::vector<uint16_t> batch_sizes { BATCH_SIZE_1, BATCH_SIZE_2 };
    std::vector<std::string> hef_paths = {"hefs/multi_network_shortcut_net.hef", "hefs/shortcut_net.hef"};

    auto configured_network_groups_exp = configure_hefs(*vdevice, hef_paths, batch_sizes);
    if (!configured_network_groups_exp) {
        std::cerr << "Failed to configure HEFs, status = " << configured_network_groups_exp.status() << std::endl;
        return configured_network_groups_exp.status();
    }
    auto configured_network_groups = configured_network_groups_exp.release();

    // Set scheduler's timeout and threshold for the first network group, it will give priority to the second network group
    auto status =  configured_network_groups[0]->set_scheduler_timeout(SCHEDULER_TIMEOUT_MS);
    if (HAILO_SUCCESS != status) {
        std::cerr << "Failed to set scheduler timeout, status = "  << status << std::endl;
        return status;
    }

    status =  configured_network_groups[0]->set_scheduler_threshold(SCHEDULER_THRESHOLD);
    if (HAILO_SUCCESS != status) {
        std::cerr << "Failed to set scheduler threshold, status = "  << status << std::endl;
        return status;
    }

    // Setting higher priority to the first network-group directly.
    // The practical meaning is that the first network will be ready to run only if ``SCHEDULER_THRESHOLD`` send requests have been accumulated, 
    // or more than ``SCHEDULER_TIMEOUT_MS`` time has passed and at least one send request has been accumulated.
    // However when both the first and the second networks are ready to run, the first network will be preferred over the second network.
    status =  configured_network_groups[0]->set_scheduler_priority(HAILO_SCHEDULER_PRIORITY_NORMAL+1);
    if (HAILO_SUCCESS != status) {
        std::cerr << "Failed to set scheduler priority, status = "  << status << std::endl;
        return status;
    }

    auto vstreams_per_network_group_exp = build_vstreams(configured_network_groups);
    if (!vstreams_per_network_group_exp) {
        std::cerr << "Failed to create vstreams, status = " << vstreams_per_network_group_exp.status() << std::endl;
        return vstreams_per_network_group_exp.status();
    }
    auto vstreams_per_network_group = vstreams_per_network_group_exp.release();

    ThreadsVector threads;
    StatusVector results;

    for (auto &vstreams_pair : vstreams_per_network_group) {
        // Create send/recv threads
        create_read_threads(vstreams_pair.second, results, threads);
        create_write_threads(vstreams_pair.first, results, threads);
    }

    // Join threads and validate results
    for (auto &thread : threads) {
        if (thread->joinable()) {
            thread->join();
        }
    }
    for (auto &status_ptr : results) {
        if (HAILO_SUCCESS != *status_ptr) {
            std::cerr << "Inference failed, status = "  << *status_ptr << std::endl;
            return *status_ptr;
        }
    }

    std::cout << "Inference finished successfully" << std::endl;
    return HAILO_SUCCESS;
}