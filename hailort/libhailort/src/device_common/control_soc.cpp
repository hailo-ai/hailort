/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file control_soc.cpp
 * @brief Implements module which allows controling Hailo SOC chip.
 **/

#include "control_soc.hpp"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"
#include "hailo/expected.hpp"
#include "hailo/hailort.h"
#include <chrono>
#include <cstdint>

namespace hailort {

hailo_power_measurement_data_t SocPowerMeasurement::get_data()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data;
}

void SocPowerMeasurement::set_data(const hailo_power_measurement_data_t &new_data)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data = new_data;
}

void SocPowerMeasurement::clear_data()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data = {};
}

hailo_status SocPowerMeasurement::stop()
{
    m_is_running = false;
    if (m_power_monitoring_thread.joinable()) {
        m_power_monitoring_thread.detach();
    }
    return HAILO_SUCCESS;
}

Expected<hailo_chip_temperature_info_t> ControlSoc::get_chip_temperature()
{
    // Chip's temperature is read from the sysfs.
    // The first hwmon (hwmon0) is the chip's temperature sensor.
    // Each file contains the respective sensor's temperature in milli degrees Celsius
    constexpr auto TS0_PATH = "/sys/class/hwmon/hwmon0/temp1_input";
    constexpr auto TS1_PATH = "/sys/class/hwmon/hwmon0/temp2_input";
    constexpr auto SAMPLE_COUNT = 1; // Always 1 sample in Hailo SOC API
    auto milli_to_celsius = [](const auto temp) { return static_cast<float32_t>(temp) / 1000; };

    TRY(auto temp0, read_number_from_file<uint32_t>(TS0_PATH));
    TRY(auto temp1, read_number_from_file<uint32_t>(TS1_PATH));

    hailo_chip_temperature_info_t data = {milli_to_celsius(temp0), milli_to_celsius(temp1), SAMPLE_COUNT};
    return hailo_chip_temperature_info_t(data);
}

// SOC's power is read from the sysfs.
// The second hwmon (hwmon1, if exists) is the SOC's power sensor.
// Returns:
//  SHUNT:   millivolt
//  BUS:     millivolt
//  POWER:   Watt
//  CURRENT: milliampere
Expected<float32_t> SocPowerMeasurement::measure(hailo_dvm_options_t dvm,
                                                 hailo_power_measurement_types_t measurement_type)
{
    CHECK((HAILO_DVM_OPTIONS_VDD_CORE == dvm) || (HAILO_DVM_OPTIONS_AUTO == dvm), HAILO_INVALID_ARGUMENT,
          "Only HAILO_DVM_OPTIONS_VDD_CORE or HAILO_DVM_OPTIONS_AUTO are supported");

    constexpr auto SHUNT_VOLTAGE_PATH = "/sys/class/hwmon/hwmon1/in0_input"; // millivolt
    constexpr auto BUS_VOLTAGE_PATH = "/sys/class/hwmon/hwmon1/in1_input";   // millivolt
    constexpr auto POWER_PATH = "/sys/class/hwmon/hwmon1/power1_input";      // microWatt
    constexpr auto CURRENT_PATH = "/sys/class/hwmon/hwmon1/curr1_input";     // milliampere
    auto milli_to_base = [](const auto milli) { return static_cast<float32_t>(milli) / 1000; };
    auto micro_to_base = [](const auto micro) { return static_cast<float32_t>(micro) / 1000000; };

    switch (measurement_type) {
    case HAILO_POWER_MEASUREMENT_TYPES__SHUNT_VOLTAGE:
    {
        TRY(auto power_value, read_number_from_file<float32_t>(SHUNT_VOLTAGE_PATH));
        return milli_to_base(power_value);
    }
    case HAILO_POWER_MEASUREMENT_TYPES__BUS_VOLTAGE:
    {
        TRY(auto power_value, read_number_from_file<float32_t>(BUS_VOLTAGE_PATH));
        return milli_to_base(power_value);
    }
    case HAILO_POWER_MEASUREMENT_TYPES__POWER:
    case HAILO_POWER_MEASUREMENT_TYPES__AUTO:
    {
        TRY(auto power_value, read_number_from_file<float32_t>(POWER_PATH));
        return micro_to_base(power_value);
    }
    case HAILO_POWER_MEASUREMENT_TYPES__CURRENT:
    {
        TRY(auto power_value, read_number_from_file<float32_t>(CURRENT_PATH));
        return power_value;
    }
    default:
    {
        LOGGER__ERROR("invalid power measurement type");
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }
    }
}

static Expected<uint32_t> translate_average_factor(uint32_t avg_factor)
{
    switch (avg_factor) {
    case HAILO_AVERAGE_FACTOR_1: return 1;
    case HAILO_AVERAGE_FACTOR_4: return 4;
    case HAILO_AVERAGE_FACTOR_16: return 16;
    case HAILO_AVERAGE_FACTOR_64: return 64;
    case HAILO_AVERAGE_FACTOR_128: return 128;
    case HAILO_AVERAGE_FACTOR_256: return 256;
    case HAILO_AVERAGE_FACTOR_512: return 512;
    case HAILO_AVERAGE_FACTOR_1024: return 1024;
    default: return make_unexpected(HAILO_INVALID_ARGUMENT);
    }
}

static Expected<uint32_t> translate_sampling_period(uint32_t sampling_period)
{
    switch (sampling_period) {
    case HAILO_SAMPLING_PERIOD_140US: return 140;
    case HAILO_SAMPLING_PERIOD_204US: return 204;
    case HAILO_SAMPLING_PERIOD_332US: return 332;
    case HAILO_SAMPLING_PERIOD_588US: return 588;
    case HAILO_SAMPLING_PERIOD_1100US: return 1100;
    case HAILO_SAMPLING_PERIOD_2116US: return 2116;
    case HAILO_SAMPLING_PERIOD_4156US: return 4156;
    case HAILO_SAMPLING_PERIOD_8244US: return 8244;
    default: return make_unexpected(HAILO_INVALID_ARGUMENT);
    }
}

hailo_status SocPowerMeasurement::config(hailo_averaging_factor_t averaging_factor,
                                         hailo_sampling_period_t sampling_period)
{
    // SOC's power is configured via the sysfs.
    // The second hwmon, if exists, (i.e. hwmon1) is the SOC's power sensor.
    constexpr auto AVG_FACTOR_PATH = "/sys/class/hwmon/hwmon1/total_average_factor";
    constexpr auto SAMPLING_PERIOD_PATH = "/sys/class/hwmon/hwmon1/total_conv_time_us";

    std::ofstream f_avg(AVG_FACTOR_PATH);
    CHECK(f_avg.is_open(), HAILO_OPEN_FILE_FAILURE, "Failed to open file: {}", AVG_FACTOR_PATH);
    TRY(auto averaging_factor_value, translate_average_factor(averaging_factor));
    f_avg << averaging_factor_value;
    CHECK(!f_avg.fail(), HAILO_FILE_OPERATION_FAILURE, "Failed to write to file: {}", AVG_FACTOR_PATH);

    std::ofstream f_samp(SAMPLING_PERIOD_PATH);
    CHECK(f_samp.is_open(), HAILO_OPEN_FILE_FAILURE, "Failed to open file: {}", SAMPLING_PERIOD_PATH);
    TRY(auto sampling_period_value, translate_sampling_period(sampling_period));
    f_samp << sampling_period_value;
    CHECK(!f_samp.fail(), HAILO_FILE_OPERATION_FAILURE, "Failed to write to file: {}", SAMPLING_PERIOD_PATH);

    m_average_factor_value = averaging_factor_value;
    m_averaging_factor = averaging_factor;
    m_sampling_period = sampling_period;
    m_sampling_period_value = sampling_period_value;
    m_sampling_interval_microseconds = 2 * sampling_period_value * averaging_factor_value;

    return HAILO_SUCCESS;
}

static hailo_power_measurement_data_t calculate_new_power_measurement_data(hailo_power_measurement_data_t old_data,
                                                                           float32_t value,
                                                                           float32_t time_delta_milliseconds)
{
    auto get_avg = [](float32_t old_avg, float32_t new_value, uint32_t new_num_of_measurement_times) {
        auto count = static_cast<float32_t>(new_num_of_measurement_times);
        return (count == 0) ? new_value : (old_avg + (new_value - old_avg) / count);
    };

    hailo_power_measurement_data_t data = old_data;
    auto is_first_sample = (0 == data.total_number_of_samples);

    data.total_number_of_samples++;
    data.average_value = is_first_sample ? value : get_avg(data.average_value, value, data.total_number_of_samples);
    data.min_value = is_first_sample ? value : std::min(data.min_value, value);
    data.max_value = is_first_sample ? value : std::max(data.max_value, value);
    data.average_time_value_milliseconds =
        get_avg(data.average_time_value_milliseconds, time_delta_milliseconds, data.total_number_of_samples);

    return data;
}

hailo_status SocPowerMeasurement::start()
{
    m_is_running = true;
    m_power_monitoring_thread = std::thread(&SocPowerMeasurement::monitor, this);
    return HAILO_SUCCESS;
}

// Monitors power consumption of the SOC
// The function is blocking and runs in a separate thread.
void SocPowerMeasurement::monitor()
{
    std::chrono::time_point<std::chrono::high_resolution_clock> old_measurement_time, new_measurement_time;

    while (m_is_running) {
        old_measurement_time = std::chrono::high_resolution_clock::now();
        std::this_thread::sleep_for(std::chrono::microseconds(m_sampling_interval_microseconds));

        auto value = measure(m_dvm, m_type);
        if (!value.has_value()) {
            LOGGER__ERROR("Monitor failed to read power measurement, status = {}", value.status());
            return;
        }

        new_measurement_time = std::chrono::high_resolution_clock::now();
        auto time_delta_milliseconds =
            std::chrono::duration<float32_t, std::milli>(new_measurement_time - old_measurement_time).count();

        std::unique_lock<std::mutex> lock(m_mutex);
        auto new_data = calculate_new_power_measurement_data(m_data, *value, time_delta_milliseconds);
        lock.unlock();

        set_data(new_data);

        old_measurement_time = new_measurement_time;
    }
}

} // namespace hailort
