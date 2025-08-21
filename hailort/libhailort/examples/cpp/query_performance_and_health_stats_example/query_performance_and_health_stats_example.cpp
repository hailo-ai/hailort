/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file query_performance_and_health_stats_example.cpp
 * This example demonstrates how to get performance and health queries from the device (Supported only on Hailo-10/Hailo-15 devices running on Linux).
 * User can pass device IDs as arguments, or if no arguments are passed, the example will scan for all available devices.
 **/

#include "hailo/hailort.hpp"

#include <iostream>

using namespace hailort;


void print_percentage(float32_t value, const std::string &label)
{
    if (-1 == value) {
        std::cout << "  - " << label << ": N/A\n";
    } else {
        std::cout << "  - " << label << ": " << value << "%\n";
    }
}

int main(int argc, char *argv[])
{
    try {
        std::vector<std::string> device_ids;
        if (argc < 2) {
            device_ids = Device::scan().expect("Failed to scan devices");
        } else {
            for (int i = 1; i < argc; i++) {
                device_ids.push_back(argv[i]);
            }
            std::cout << device_ids.size() << " device(s) passed as arguments" << std::endl;
        }
        std::cout << "Found " << device_ids.size() << " device(s)" << std::endl;

        for(const auto &device_id : device_ids) {
            auto physical_device = Device::create(device_id).expect("Failed to create device");

            std::cout << "\n------------------------------------------------------------------\n";
            std::cout << "--- Query performance and health stats on device: " << device_id << " ---\n";
            std::cout << "------------------------------------------------------------------\n";

            auto performance_stats = physical_device->query_performance_stats().expect("Failed to query performance stats");
            std::cout << "Performance stats:\n";
            print_percentage(performance_stats.cpu_utilization, "CPU utilization");
            auto ram_utilization = (100.0f * static_cast<float32_t>(performance_stats.ram_size_used)) / static_cast<float32_t>(performance_stats.ram_size_total);
            std::cout << "  - RAM utilization: " << ram_utilization << "%\n";
            print_percentage(performance_stats.nnc_utilization, "NNC utilization");
            std::cout << "  - DDR NOC total transactions: " << performance_stats.ddr_noc_total_transactions << "\n";
            print_percentage(static_cast<float32_t>(performance_stats.dsp_utilization), "DSP utilization");

            auto health_stats = physical_device->query_health_stats().expect("Failed to query health stats");
            std::cout << "Health stats:\n";
            std::cout << "  - On die temperature: " << health_stats.on_die_temperature << "\n";
            std::cout << "  - On die voltage: " << health_stats.on_die_voltage << "\n";
            if (-1 != health_stats.bist_failure_mask) {
                std::cout << "  - BIST failure mask: 0x" << std::hex << std::uppercase << health_stats.bist_failure_mask << std::dec << "\n";
            } else {
                std::cout << "  - Startup BIST failure mask: -1\n";
            }
        }

        std::cout << "Query stats finished successfully" << std::endl;

    } catch (const hailort_error &exception) {
        std::cout << "Failed query stats. status=" << exception.status() << ", error message: " << exception.what() << std::endl;
        return exception.status();
    };

    return HAILO_SUCCESS;
}