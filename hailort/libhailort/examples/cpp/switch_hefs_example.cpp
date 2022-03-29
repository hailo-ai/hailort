/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @ file switch_hefs_example.cpp
 * This example demonstrates basic usage of HailoRT streaming api over multiple network groups, using vstreams.
 * It loads several HEF networks with single/multiple inputs and single/multiple outputs into a Hailo PCIe VDevice and performs a
 * short inference on each one. 
 * After inference is finished, the example switches to the next HEF and start inference again.
 **/

#include "hailo/hailort.hpp"

#include <iostream>
#include <chrono>

constexpr bool QUANTIZED = true;
constexpr hailo_format_type_t FORMAT_TYPE = HAILO_FORMAT_TYPE_AUTO;
constexpr size_t INFER_FRAME_COUNT = 100;
constexpr size_t RUN_COUNT = 10;
constexpr uint32_t DEVICE_COUNT = 1;


using hailort::VDevice;
using hailort::Hef;
using hailort::Expected;
using hailort::make_unexpected;
using hailort::ConfiguredNetworkGroup;
using hailort::VStreamsBuilder;
using hailort::InputVStream;
using hailort::OutputVStream;
using hailort::MemoryView;


void write_all(InputVStream &input_vstream, hailo_status &status_out)
{
    std::vector<uint8_t> buff(input_vstream.get_frame_size());

    for (size_t i = 0; i < INFER_FRAME_COUNT; i++) {
        auto status = input_vstream.write(MemoryView(buff.data(), buff.size()));
        if (HAILO_SUCCESS != status) {
            status_out = status;
            return;
        }
    }
    return;
}

void read_all(OutputVStream &output_vstream, hailo_status &status_out)
{
    std::vector<uint8_t> buff(output_vstream.get_frame_size());

    for (size_t i = 0; i < INFER_FRAME_COUNT; i++) {
        auto status = output_vstream.read(MemoryView(buff.data(), buff.size()));
        if (HAILO_SUCCESS != status) {
            status_out = status;
            return;
        }
    }
    return;
}

Expected<std::vector<std::pair<std::vector<InputVStream>, std::vector<OutputVStream>>>> build_vstreams(
    const std::vector<std::shared_ptr<ConfiguredNetworkGroup>> &configured_network_groups)
{
    std::vector<std::pair<std::vector<InputVStream>, std::vector<OutputVStream>>> vstreams_per_network_group;

    for (auto &network_group : configured_network_groups) {
        auto vstreams_exp = VStreamsBuilder::create_vstreams(*network_group, QUANTIZED, FORMAT_TYPE);
        if (!vstreams_exp) {
            return make_unexpected(vstreams_exp.status());
        }
        vstreams_per_network_group.emplace_back(vstreams_exp.release());
    }
    return vstreams_per_network_group;
}

std::vector<std::unique_ptr<std::thread>> create_read_threads(std::vector<OutputVStream> &vstreams,
    std::vector<hailo_status> &read_results)
{
    std::vector<std::unique_ptr<std::thread>> read_threads;

    read_results.reserve(vstreams.size());
    for (auto &vstream : vstreams) {
        read_results.push_back(HAILO_SUCCESS); // Success oriented
        read_threads.emplace_back(std::make_unique<std::thread>(read_all,
            std::ref(vstream), std::ref(read_results.back())));
    }
    return read_threads;
}

std::vector<std::unique_ptr<std::thread>> create_write_threads(std::vector<InputVStream> &vstreams,
    std::vector<hailo_status> &write_results)
{
    std::vector<std::unique_ptr<std::thread>> write_threads;

    write_results.reserve(vstreams.size());
    for (auto &vstream : vstreams) {
        write_results.push_back(HAILO_SUCCESS); // Success oriented
        write_threads.emplace_back(std::make_unique<std::thread>(write_all,
            std::ref(vstream), std::ref(write_results.back())));
    }
    return write_threads;
}

int main()
{
    hailo_vdevice_params_t params;
    auto status = hailo_init_vdevice_params(&params);
    if (HAILO_SUCCESS != status) {
        std::cerr << "Failed init vdevice_params, status = " << status << std::endl;
        return status;
    }

    params.device_count = DEVICE_COUNT;
    auto vdevice_exp = VDevice::create(params);
    if (!vdevice_exp) {
        std::cerr << "Failed create vdevice, status = " << vdevice_exp.status() << std::endl;
        return vdevice_exp.status();
    }
    auto vdevice = vdevice_exp.release();

    std::vector<std::string> hef_paths = {"hefs/shortcut_net.hef", "hefs/shortcut_net.hef"};
    std::vector<std::shared_ptr<ConfiguredNetworkGroup>> configured_network_groups;

    for (const auto &path : hef_paths) {
        auto hef_exp = Hef::create(path);
        if (!hef_exp) {
            std::cerr << "Failed to create hef: " << path  << ", status = " << hef_exp.status() << std::endl;
            return hef_exp.status();
        }
        auto hef = hef_exp.release();

        auto added_network_groups = vdevice->configure(hef);
        if (!added_network_groups) {
            std::cerr << "Failed to configure vdevice, status = " << added_network_groups.status() << std::endl;
            return added_network_groups.status();
        }
        configured_network_groups.insert(configured_network_groups.end(), added_network_groups->begin(),
            added_network_groups->end());
    }

    auto vstreams_per_network_group_exp = build_vstreams(configured_network_groups);
    if (!vstreams_per_network_group_exp) {
        std::cerr << "Failed to create vstreams, status = " << vstreams_per_network_group_exp.status() << std::endl;
        return vstreams_per_network_group_exp.status();
    }
    auto vstreams_per_network_group = vstreams_per_network_group_exp.release();

    for (size_t i = 0; i < RUN_COUNT; i++) {
        for (size_t network_group_idx = 0; network_group_idx < configured_network_groups.size(); network_group_idx++) {
            auto activated_network_group_exp = configured_network_groups[network_group_idx]->activate();

            if (!activated_network_group_exp) {
                std::cerr << "Failed to activate network group, status = "  << activated_network_group_exp.status() << std::endl;
                return activated_network_group_exp.status();
            }

            // Create send/recv threads
            std::vector<hailo_status> read_results;
            auto read_threads = create_read_threads(vstreams_per_network_group[network_group_idx].second, read_results);

            std::vector<hailo_status> write_results;
            auto write_threads = create_write_threads(vstreams_per_network_group[network_group_idx].first, write_results);

            // Join threads and validate results
            for (auto &th : write_threads) {
                if (th->joinable()) {
                    th->join();
                }
            }
            for (auto &th : read_threads) {
                if (th->joinable()) {
                    th->join();
                }
            }

            for (auto &thread_status : write_results) {
                if (HAILO_SUCCESS != thread_status) {
                    std::cerr << "Inference failed, status = "  << thread_status << std::endl;
                    return thread_status;
                }
            }
            for (auto &thread_status : read_results) {
                if (HAILO_SUCCESS != thread_status) {
                    std::cerr << "Inference failed, status = "  << thread_status << std::endl;
                    return thread_status;
                }
            }
        }
    }

    std::cout << "Inference finished successfully" << std::endl;
    return HAILO_SUCCESS;
}