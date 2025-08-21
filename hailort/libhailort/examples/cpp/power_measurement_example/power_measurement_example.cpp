/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file power_measurement_example.cpp
 * This example demonstrates power and current measurements.
 **/

#include "hailo/hailort.hpp"

#include <iostream>
#include <chrono>
#include <thread>


#define SAMPLING_PERIOD (HAILO_SAMPLING_PERIOD_1100US)
#define AVERAGE_FACTOR (HAILO_AVERAGE_FACTOR_256)
#define DVM_OPTION (HAILO_DVM_OPTIONS_AUTO) // For current measurement over EVB - pass DVM explicitly (see hailo_dvm_options_t)
#define MEASUREMENT_BUFFER_INDEX (HAILO_MEASUREMENT_BUFFER_INDEX_0)

#define MEASUREMENT_UNITS(__type) \
    ((HAILO_POWER_MEASUREMENT_TYPES__POWER == __type) ? ("W") : ("mA"))

#define USAGE_ERROR_MSG ("Args parsing error.\nUsage: power_measurement_example [power / current]\n" \
    "* power   - measure power consumption in W\n" \
    "* current - measure current in mA\n")

const std::string POWER_ARG = "power";
const std::string CURRENT_ARG = "current";

const std::chrono::seconds MEASUREMENTS_DURATION_SECS(1);

using namespace hailort;

Expected<hailo_power_measurement_types_t> parse_arguments(int argc, char **argv)
{
    if (2 != argc) {
        std::cerr << USAGE_ERROR_MSG << std::endl;
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    if (POWER_ARG == std::string(argv[1])) {
        return HAILO_POWER_MEASUREMENT_TYPES__POWER;
    } else if (CURRENT_ARG == std::string(argv[1]))  {
        return HAILO_POWER_MEASUREMENT_TYPES__CURRENT;
    } else {
        std::cerr << USAGE_ERROR_MSG << std::endl;
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }
}

void print_measurements_results(Device &device, const hailo_power_measurement_data_t &result, hailo_power_measurement_types_t type)
{
    auto id = device.get_dev_id();

    auto type_str = (type == HAILO_POWER_MEASUREMENT_TYPES__POWER) ? "Power measurement" :
        "Current measurement";

    std::cout << "Device " << std::string(id) << ":" << std::endl;
    std::cout << "  " << type_str << std::endl;
    std::cout << "    Minimum value: " << result.min_value << MEASUREMENT_UNITS(type) << std::endl;
    std::cout << "    Average value: " << result.average_value << MEASUREMENT_UNITS(type) << std::endl;
    std::cout << "    Maximum value: " << result.max_value << MEASUREMENT_UNITS(type) << std::endl;
}

int main(int argc, char **argv)
{
    auto measurement_type = parse_arguments(argc, argv);
    if (!measurement_type) {
        return measurement_type.status();
    }

    auto scan_res = Device::scan_pcie();
    if (!scan_res) {
        std::cerr << "Failed to scan pcie_device" << std::endl;
        return scan_res.status();
    }

    hailo_vdevice_params_t params;
    auto status = hailo_init_vdevice_params(&params);
    if (HAILO_SUCCESS != status) {
        std::cerr << "Failed init vdevice_params, status = " << status << std::endl;
        return status;
    }

    /* Scheduler over multiple devices is currently not supported */
    params.scheduling_algorithm = HAILO_SCHEDULING_ALGORITHM_NONE;

    params.device_count = static_cast<uint32_t>(scan_res->size());
    auto vdevice = VDevice::create(params);
    if (!vdevice) {
        std::cerr << "Failed create vdevice, status = " << vdevice.status() << std::endl;
        return vdevice.status();
    }

    auto physical_devices = vdevice.value()->get_physical_devices();
    if (!physical_devices) {
        std::cerr << "Failed to get physical devices" << std::endl;
        return physical_devices.status();
    }

    for (auto &physical_device : physical_devices.value()) {
        auto device_capabilities = physical_device.get().get_capabilities();
        if (!device_capabilities) {
            std::cerr << "Failed to get device capabilities for device " << physical_device.get().get_dev_id() << std::endl;
            return device_capabilities.status();
        }

        if (!device_capabilities->power_measurements) {
            std::cout << "Power measurement is not supported on device " << physical_device.get().get_dev_id() << std::endl;
            return HAILO_NOT_SUPPORTED;
        }

        status = physical_device.get().stop_power_measurement();
        if (HAILO_SUCCESS != status) {
            std::cerr << "Failed stopping former measurements" << std::endl;
            return status;
        }

        status = physical_device.get().set_power_measurement(MEASUREMENT_BUFFER_INDEX, DVM_OPTION, measurement_type.value());
        if (HAILO_SUCCESS != status) {
            std::cerr << "Failed setting measurement params" << std::endl;
            return status;
        }

        status = physical_device.get().start_power_measurement(AVERAGE_FACTOR, SAMPLING_PERIOD);
        if (HAILO_SUCCESS != status) {
            std::cerr << "Failed to start measurement" << std::endl;
            return status;
        }
    }

    std::this_thread::sleep_for(MEASUREMENTS_DURATION_SECS);

    for (auto &physical_device : physical_devices.value()) {
        status = physical_device.get().stop_power_measurement();
        if (HAILO_SUCCESS != status) {
            std::cerr << "Failed to stop measurement" << std::endl;
            return status;
        }

        auto measurement_result = physical_device.get().get_power_measurement(MEASUREMENT_BUFFER_INDEX, true);
        if (!measurement_result) {
            std::cerr << "Failed to get measurement results" << std::endl;
            return measurement_result.status();
        }

        print_measurements_results(physical_device.get(), measurement_result.value(), measurement_type.value());
    }

    return HAILO_SUCCESS;
}
