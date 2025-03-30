/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file power_measurement_command.cpp
 * @brief Measure power of Hailo chip
 **/

#include "power_measurement_command.hpp"
#include <thread>


PowerMeasurementSubcommand::PowerMeasurementSubcommand(CLI::App &parent_app) :
    DeviceCommand(parent_app.add_subcommand("measure-power", "Measures power consumption")), m_params(),
    m_power_measurement_duration(0)
{
    m_app->add_option("--duration", m_power_measurement_duration, "The duration in seconds to measure power consumption")
        ->check(CLI::Validator(CLI::PositiveNumber));

    CLI::Option *sampling_period = m_app->add_option("--sampling-period", m_params.sampling_period, "Sampling Period")
        ->needs("--duration");
    CLI::Option *averaging_factor = m_app->add_option("--averaging-factor", m_params.averaging_factor, "Averaging Factor")
        ->needs("--duration");
    init_dvm_option(m_app->add_option("--dvm", m_params.dvm_option,
                "DVM type. \n\
Which DVM will be measured. Default (AUTO) will be different according to the board: \n\
Default (AUTO) for EVB is an approximation to the total power consumption of the chip in PCIe setups. \n\
It sums VDD_CORE, MIPI_AVDD and AVDD_H. Only POWER can measured with this option. \n\
Default (AUTO) for platforms supporting current monitoring (such as M.2 and mPCIe): OVERCURRENT_PROTECTION"));
    init_measurement_type_option(m_app->add_option("--type", m_params.measurement_type, "Power Measurement type"));
    init_sampling_period_option(sampling_period);
    init_averaging_factor_option(averaging_factor);
}

void PowerMeasurementSubcommand::init_dvm_option(CLI::Option *dvm_option)
{
    dvm_option->transform(HailoCheckedTransformer<hailo_dvm_options_t>({
            { "AUTO", HAILO_DVM_OPTIONS_AUTO },
            { "VDD_CORE", HAILO_DVM_OPTIONS_VDD_CORE },
            { "VDD_IO", HAILO_DVM_OPTIONS_VDD_IO },
            { "MIPI_AVDD", HAILO_DVM_OPTIONS_MIPI_AVDD },
            { "MIPI_AVDD_H", HAILO_DVM_OPTIONS_MIPI_AVDD_H },
            { "USB_AVDD_IO", HAILO_DVM_OPTIONS_USB_AVDD_IO },
            { "VDD_TOP", HAILO_DVM_OPTIONS_VDD_TOP },
            { "USB_AVDD_IO_HV", HAILO_DVM_OPTIONS_USB_AVDD_IO_HV },
            { "AVDD_H", HAILO_DVM_OPTIONS_AVDD_H },
            { "SDIO_VDD_IO", HAILO_DVM_OPTIONS_SDIO_VDD_IO },
            { "OVERCURRENT_PROTECTION", HAILO_DVM_OPTIONS_OVERCURRENT_PROTECTION }
        }))
        ->default_val("AUTO");
}

void PowerMeasurementSubcommand::init_measurement_type_option(CLI::Option *measurement_type)
{
    measurement_type->transform(HailoCheckedTransformer<hailo_power_measurement_types_t>({
            { "AUTO", HAILO_POWER_MEASUREMENT_TYPES__AUTO },
            { "SHUNT_VOLTAGE", HAILO_POWER_MEASUREMENT_TYPES__SHUNT_VOLTAGE },
            { "BUS_VOLTAGE", HAILO_POWER_MEASUREMENT_TYPES__BUS_VOLTAGE },
            { "POWER", HAILO_POWER_MEASUREMENT_TYPES__POWER },
            { "CURRENT", HAILO_POWER_MEASUREMENT_TYPES__CURRENT }
        }))
        ->default_val("AUTO");
}

void PowerMeasurementSubcommand::init_sampling_period_option(CLI::Option *sampling_period)
{
    sampling_period->default_val(1100)
        ->transform(CLI::IsMember({140, 204, 332, 588, 1100, 2116, 4156, 8244}));
}

void PowerMeasurementSubcommand::init_averaging_factor_option(CLI::Option *averaging_factor)
{
    averaging_factor->default_val(256)
        ->transform(CLI::IsMember({1, 4, 16, 64, 128, 256, 512, 1024}));
}

hailo_status PowerMeasurementSubcommand::execute_on_device(Device &device)
{
    hailo_status status = HAILO_UNINITIALIZED;

    if (0 < m_power_measurement_duration) {
        status = run_long_power_measurement(device);
    } else {
        status = run_single_power_measurement(device);
    }

    if (HAILO_SUCCESS != status) {
        std::cerr << "Failed power measurement, status " << status << std::endl;
        return status;
    }

    return HAILO_SUCCESS;
}

Expected<LongPowerMeasurement> PowerMeasurementSubcommand::start_power_measurement(Device &device,
    hailo_dvm_options_t dvm, hailo_power_measurement_types_t measurement_type, uint32_t sampling_period,
    uint32_t averaging_factor)
{
    hailo_sampling_period_t sampling_period_enum = get_sampling_period(
        sampling_period);
    if (HAILO_SAMPLING_PERIOD_MAX_ENUM == sampling_period_enum) {
        std::cerr << "Failed to parse sampling period: " << sampling_period << std::endl;
        return make_unexpected(HAILO_NOT_FOUND);
    }

    hailo_averaging_factor_t averaging_factor_enum = get_averaging_factor(
        averaging_factor);
    if (HAILO_AVERAGE_FACTOR_MAX_ENUM == averaging_factor_enum) {
        std::cerr << "Failed to parse averaging factor: " << averaging_factor << std::endl;
        return make_unexpected(HAILO_NOT_FOUND);
    }

    hailo_status status = device.stop_power_measurement();
    if (HAILO_SUCCESS != status) {
        std::cerr << "Failed initial power measurement stop, status " << status << std::endl;
        return make_unexpected(status);
    }

    status = device.set_power_measurement(HAILO_MEASUREMENT_BUFFER_INDEX_0, dvm, measurement_type);
    if (HAILO_SUCCESS != status) {
        std::cerr << "Failed to set power measurement parameters, status " << status << std::endl;
        return make_unexpected(status);
    }

    status = device.start_power_measurement(averaging_factor_enum, sampling_period_enum);
    if (HAILO_SUCCESS != status) {
        std::cerr << "Failed to start power measurement, status " << status << std::endl;
        return make_unexpected(status);
    }

    return LongPowerMeasurement(device, measurement_type);
}

LongPowerMeasurement::LongPowerMeasurement(Device &device,
    hailo_power_measurement_types_t measurement_type) : m_device(device),
    m_measurement_type(measurement_type), m_data(), m_power_units()
{}

hailo_status LongPowerMeasurement::stop()
{
    hailo_status status = m_device.stop_power_measurement();
    if (HAILO_SUCCESS != status) {
        std::cerr << "Failed to stop power measurement, status " << status << std::endl;
        return status;
    }

    TRY(m_data, m_device.get_power_measurement(HAILO_MEASUREMENT_BUFFER_INDEX_0, true));

    const char *power_units = PowerMeasurementSubcommand::get_power_units(m_measurement_type);
    if (nullptr == power_units) {
        std::cerr << "Failed to get power measurement units of type " << m_measurement_type << std::endl;
        return HAILO_NOT_FOUND;
    }
    m_power_units = power_units;

    return HAILO_SUCCESS;
}

hailo_sampling_period_t PowerMeasurementSubcommand::get_sampling_period(uint32_t sampling_period)
{
    switch (sampling_period) {
    case 140:
        return HAILO_SAMPLING_PERIOD_140US;
    case 204:
        return HAILO_SAMPLING_PERIOD_204US;
    case 332:
        return HAILO_SAMPLING_PERIOD_332US;
    case 588:
        return HAILO_SAMPLING_PERIOD_588US;
    case 1100:
        return HAILO_SAMPLING_PERIOD_1100US;
    case 2116:
        return HAILO_SAMPLING_PERIOD_2116US;
    case 4156:
        return HAILO_SAMPLING_PERIOD_4156US;
    case 8244:
        return HAILO_SAMPLING_PERIOD_8244US;
    default:
        return HAILO_SAMPLING_PERIOD_MAX_ENUM;
    }
}

hailo_averaging_factor_t PowerMeasurementSubcommand::get_averaging_factor(uint32_t averaging_factor)
{
    switch (averaging_factor) {
    case 1:
        return HAILO_AVERAGE_FACTOR_1;
    case 4:
        return HAILO_AVERAGE_FACTOR_4;
    case 16:
        return HAILO_AVERAGE_FACTOR_16;
    case 64:
        return HAILO_AVERAGE_FACTOR_64;
    case 128:
        return HAILO_AVERAGE_FACTOR_128;
    case 256:
        return HAILO_AVERAGE_FACTOR_256;
    case 512:
        return HAILO_AVERAGE_FACTOR_512;
    case 1024:
        return HAILO_AVERAGE_FACTOR_1024;
    default:
        return HAILO_AVERAGE_FACTOR_MAX_ENUM;
    }
}

hailo_status PowerMeasurementSubcommand::run_long_power_measurement(Device &device)
{
    auto long_power_measurement = start_power_measurement(device, m_params.dvm_option,
        m_params.measurement_type, m_params.sampling_period, m_params.averaging_factor);
    if (!long_power_measurement) {
        return long_power_measurement.status();
    }

    std::this_thread::sleep_for(std::chrono::seconds(m_power_measurement_duration));

    hailo_status status = long_power_measurement.value().stop();
    if (HAILO_SUCCESS != status) {
        return status;
    }

    const hailo_power_measurement_data_t &measurement_data = long_power_measurement.value().data();
    const std::string &power_units = long_power_measurement.value().power_units();

    std::cout << "Measuring power consumption over " << m_power_measurement_duration << " seconds:" << std::endl;
    std::cout << "Total samples: " << measurement_data.total_number_of_samples << std::endl;
    std::cout << "Max value (" << power_units << "): " << measurement_data.max_value << std::endl;
    std::cout << "Min value (" << power_units << "): " << measurement_data.min_value << std::endl;
    std::cout << "Average value (" << power_units << "): " << measurement_data.average_value << std::endl;
    std::cout << "Average time per sample (ms): " << measurement_data.average_time_value_milliseconds << std::endl;
    return HAILO_SUCCESS;
}

const char *PowerMeasurementSubcommand::get_power_units(hailo_power_measurement_types_t measurement_type)
{
    switch (measurement_type) {
    case HAILO_POWER_MEASUREMENT_TYPES__SHUNT_VOLTAGE:
    case HAILO_POWER_MEASUREMENT_TYPES__BUS_VOLTAGE:
        return "mV";
    case HAILO_POWER_MEASUREMENT_TYPES__AUTO:
    case HAILO_POWER_MEASUREMENT_TYPES__POWER:
        return "W";
    case HAILO_POWER_MEASUREMENT_TYPES__CURRENT:
        return "mA";
    default:
        return nullptr;
    };
}

hailo_status PowerMeasurementSubcommand::run_single_power_measurement(Device &device)
{
    TRY(auto measurement, device.power_measurement(m_params.dvm_option, m_params.measurement_type));

    const char *power_units = get_power_units(m_params.measurement_type);
    if (nullptr == power_units) {
        std::cerr << "Failed to get power measurement units of type " << m_params.measurement_type << std::endl;
        return HAILO_NOT_FOUND;
    }

    std::cout << "Current power consumption (" << power_units << "): " << measurement << std::endl;
    return HAILO_SUCCESS;
}
