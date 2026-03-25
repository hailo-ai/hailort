/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file accelerator_monitor.cpp
 * @brief Accelerator device stats monitor for H10 devices
 **/

#include "accelerator_monitor.hpp"
#include "hailortcli.hpp"
#include "common.hpp"
#include "monitor_common.hpp"
#include "utils/profiler/monitor_handler.hpp"

#include <iostream>
#include <thread>

namespace hailort
{

// Table column widths
constexpr size_t COL_ID_WIDTH = 22;
constexpr size_t COL_ARCH_WIDTH = 15;
constexpr size_t COL_PERCENT_WIDTH = 22;
constexpr size_t COL_RAM_USAGE_WIDTH = 18;
constexpr size_t COL_TEMP_WIDTH = 25;
constexpr size_t COL_VOLTAGE_WIDTH = 22;
constexpr size_t LINE_LENGTH = COL_ID_WIDTH + COL_ARCH_WIDTH + (3 * COL_PERCENT_WIDTH) + COL_RAM_USAGE_WIDTH +
    COL_TEMP_WIDTH + COL_VOLTAGE_WIDTH;
constexpr std::chrono::milliseconds PERF_SAMPLING_PERIOD(1000);
static std::string format_float_stat(float32_t value)
{
    if (SCHEDULER_MON_INVALID_VALUE == value) {
        return "N/A";
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << value;
    return ss.str();
}

static std::string format_int_stat(int32_t value)
{
    if (SCHEDULER_MON_INVALID_VALUE == value) {
        return "N/A";
    }
    return std::to_string(value);
}

void AcceleratorMonitor::add_table_header(std::ostream &buffer)
{
    buffer <<
        std::setw(COL_ID_WIDTH) << std::left << "Device ID" <<
        std::setw(COL_ARCH_WIDTH) << std::left << "Architecture" <<
        std::setw(COL_PERCENT_WIDTH) << std::left << "NNC Utilization (%)" <<
        std::setw(COL_PERCENT_WIDTH) << std::left << "CPU Utilization (%)" <<
        std::setw(COL_PERCENT_WIDTH) << std::left << "RAM Utilization (%)" <<
        std::setw(COL_RAM_USAGE_WIDTH) << std::left << "RAM Usage (MB)" <<
        std::setw(COL_TEMP_WIDTH) << std::left << "On Die Temperature (C)" <<
        std::setw(COL_VOLTAGE_WIDTH) << std::left << "On Die Voltage (mV)" <<
        "\n" << std::left << std::string(LINE_LENGTH, '-') << "\n";
}

void AcceleratorMonitor::add_table_row(const MonitorEntry &entry, std::ostream &buffer)
{
    buffer <<
        std::setw(COL_ID_WIDTH) << std::left << entry.device_id <<
        std::setw(COL_ARCH_WIDTH) << std::left << entry.architecture <<
        std::setw(COL_PERCENT_WIDTH) << std::left << format_float_stat(entry.nnc_utilization) <<
        std::setw(COL_PERCENT_WIDTH) << std::left << format_float_stat(entry.cpu_utilization) <<
        std::setw(COL_PERCENT_WIDTH) << std::left << format_float_stat(entry.ram_utilization) <<
        std::setw(COL_RAM_USAGE_WIDTH) << std::left <<
            ((SCHEDULER_MON_INVALID_VALUE == entry.ram_used) ? "N/A" : (std::to_string(entry.ram_used) + " / " + std::to_string(entry.ram_total))) <<
        std::setw(COL_TEMP_WIDTH) << std::left << format_float_stat(entry.on_die_temperature) <<
        std::setw(COL_VOLTAGE_WIDTH) << std::left << format_int_stat(entry.on_die_voltage) <<
        "\n";
}

hailo_status AcceleratorMonitor::print_table(const std::vector<MonitorEntry> &entries)
{
    std::ostringstream buffer;
    buffer << FORMAT_RESET_TERMINAL_CURSOR_FIRST_LINE;

    add_table_header(buffer);
    for (const auto &entry : entries) {
        add_table_row(entry, buffer);
    }

    std::cout << buffer.str() << std::flush;
    return HAILO_SUCCESS;
}

std::vector<std::unique_ptr<Device>> AcceleratorMonitor::scan_h10_devices()
{
    std::vector<std::unique_ptr<Device>> h10_devices;
    auto scan_result = Device::scan();
    if ((HAILO_SUCCESS != scan_result.status()) || scan_result->empty()) {
        return h10_devices;
    }

    for (const auto &device_id : scan_result.value()) {
        auto device = Device::create(device_id);
        if (HAILO_SUCCESS != device.status()) {
            continue;
        }
        auto arch = device.value()->get_architecture();
        if (HAILO_SUCCESS != arch.status()) {
            continue;
        }
        if (HAILO_ARCH_HAILO10H == arch.value()) {
            h10_devices.emplace_back(device.release());
        }
    }
    return h10_devices;
}

hailo_status AcceleratorMonitor::run()
{
    InterruptHandler::register_handler();

    AlternativeTerminal alt_terminal;
    while (InterruptHandler::is_running()) {
        auto devices = scan_h10_devices();
        std::vector<MonitorEntry> entries;
        entries.reserve(devices.size());

        for (auto &device : devices) {
            MonitorEntry entry;
            entry.device_id = device->get_dev_id();

            auto arch = device->get_architecture();
            if (HAILO_SUCCESS == arch.status()) {
                entry.architecture = HailoRTCommon::get_device_arch_str(arch.value());
            } else {
                entry.architecture = "N/A";
            }

            // Query performance stats (blocks for ~PERF_SAMPLING_PERIOD)
            auto perf_stats = device->query_performance_stats(PERF_SAMPLING_PERIOD);
            if (perf_stats.has_value()) {
                entry.cpu_utilization = perf_stats->cpu_utilization;
                entry.nnc_utilization = perf_stats->nnc_utilization;
                entry.ram_total = perf_stats->ram_size_total / 1024;
                entry.ram_used = perf_stats->ram_size_used / 1024;
                if (perf_stats->ram_size_total > 0 && perf_stats->ram_size_used >= 0) {
                    entry.ram_utilization = (100.0f * static_cast<float32_t>(perf_stats->ram_size_used)) /
                                            static_cast<float32_t>(perf_stats->ram_size_total);
                } else {
                    entry.ram_utilization = SCHEDULER_MON_INVALID_VALUE;
                }
            } else {
                entry.cpu_utilization = SCHEDULER_MON_INVALID_VALUE;
                entry.nnc_utilization = SCHEDULER_MON_INVALID_VALUE;
                entry.ram_utilization = SCHEDULER_MON_INVALID_VALUE;
                entry.ram_total = SCHEDULER_MON_INVALID_VALUE;
                entry.ram_used = SCHEDULER_MON_INVALID_VALUE;
            }

            // Query health stats (non-blocking)
            auto health_stats = device->query_health_stats();
            if (health_stats.has_value()) {
                entry.on_die_temperature = health_stats->on_die_temperature;
                entry.on_die_voltage = health_stats->on_die_voltage;
            } else {
                entry.on_die_temperature = SCHEDULER_MON_INVALID_VALUE;
                entry.on_die_voltage = SCHEDULER_MON_INVALID_VALUE;
            }

            entries.emplace_back(std::move(entry));
        }

        CHECK_SUCCESS(print_table(entries));
        // No additional sleep needed: query_performance_stats already blocks for ~1 second
    }

    return HAILO_SUCCESS;
}

} /* namespace hailort */
