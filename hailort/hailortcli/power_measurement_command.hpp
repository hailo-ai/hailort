/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file power_measurement_command.hpp
 * @brief Measure power of Hailo chip
 **/

#ifndef _HAILO_POWER_MEASUREMENT_COMMAND_HPP_
#define _HAILO_POWER_MEASUREMENT_COMMAND_HPP_

#include "hailortcli.hpp"
#include "command.hpp"
#include "hailo/hailort.h"
#include "hailo/device.hpp"
#include "CLI/CLI.hpp"

class LongPowerMeasurement final {
public:
    LongPowerMeasurement(Device &device, hailo_power_measurement_types_t measurement_type);
    ~LongPowerMeasurement() = default;
    hailo_status stop();

    const hailo_power_measurement_data_t &data() const
    {
        return m_data;
    }

    const std::string &power_units() const
    {
        return m_power_units;
    }

private:
    Device &m_device;
    hailo_power_measurement_types_t m_measurement_type;
    hailo_power_measurement_data_t m_data;
    std::string m_power_units;
};

class PowerMeasurementSubcommand final : public DeviceCommand {
public:
    explicit PowerMeasurementSubcommand(CLI::App &parent_app);

    static Expected<LongPowerMeasurement> start_power_measurement(Device &device,
        hailo_dvm_options_t dvm, hailo_power_measurement_types_t measurement_type,
        uint32_t sampling_period, uint32_t averaging_factor);
    static void init_sampling_period_option(CLI::Option *sampling_period);
    static void init_averaging_factor_option(CLI::Option *averaging_factor);
    static hailo_sampling_period_t get_sampling_period(uint32_t sampling_period);
    static hailo_averaging_factor_t get_averaging_factor(uint32_t averaging_factor);
    static const char *get_power_units(hailo_power_measurement_types_t measurement_type);

protected:
    hailo_status execute_on_device(Device &device) override;

private:
    struct power_measurement_params {
        hailo_dvm_options_t dvm_option;
        hailo_power_measurement_types_t measurement_type;
        uint32_t sampling_period;
        uint32_t averaging_factor;
    };

    power_measurement_params m_params;
    uint32_t m_power_measurement_duration;

    static void init_dvm_option(CLI::Option *dvm_option);
    static void init_measurement_type_option(CLI::Option *measurement_type);
    hailo_status run_long_power_measurement(Device &device);
    hailo_status run_single_power_measurement(Device &device);
};

#endif /* _HAILO_POWER_MEASUREMENT_COMMAND_HPP_ */
