/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file temp_measurement.cpp
 * @brief Measure temperature of Hailo chip
 **/

#include "temp_measurement.hpp"

constexpr std::chrono::milliseconds DEFAULT_TEMPERATURE_MEASUREMENTS_INTERVAL(1000);

static float32_t calc_avg(uint32_t old_samples_count, float32_t old_avg, uint32_t new_samples_count, float32_t new_value)
{
    float32_t old_samples = static_cast<float32_t>(old_samples_count);
    float32_t new_samples = static_cast<float32_t>(new_samples_count);
    float32_t total_samples_count = old_samples + new_samples;
    return (((old_avg * old_samples) + (new_value * new_samples)) / total_samples_count);
}

TemperatureMeasurement::TemperatureMeasurement(Device &device) :
    m_device(device),
    m_is_thread_running(false),
    m_data()
{}

TemperatureMeasurement::~TemperatureMeasurement()
{
    stop_measurement();
}

hailo_status TemperatureMeasurement::start_measurement()
{
    // Checking temperature sensor before starting thread
    auto temp_info = m_device.get_chip_temperature();
    CHECK_EXPECTED_AS_STATUS(temp_info);

    m_is_thread_running = true;
    m_thread = std::thread([this] () {
        while (m_is_thread_running.load()) {
            auto temp_info = m_device.get_chip_temperature();
            if (temp_info.status() != HAILO_SUCCESS) {
                LOGGER__ERROR("Failed to get chip's temperature, status = {}", temp_info.status());
                m_is_thread_running = false;
                break;
            }

            TempMeasurementData new_data = {};
            auto old_data = m_data;

            float32_t ts_avg = ((temp_info->ts0_temperature + temp_info->ts1_temperature) / 2);
            new_data.max_value  = std::max(old_data.max_value, ts_avg);
            new_data.min_value  = (old_data.min_value == 0) ? ts_avg : std::min(old_data.min_value, ts_avg);
            new_data.average_value = calc_avg(old_data.sample_count, old_data.average_value, temp_info->sample_count, ts_avg);
            new_data.sample_count = old_data.sample_count + temp_info->sample_count;

            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_data = new_data;
            }
            
            std::this_thread::sleep_for(DEFAULT_TEMPERATURE_MEASUREMENTS_INTERVAL); 
        }
    });

    return HAILO_SUCCESS;
}

void TemperatureMeasurement::stop_measurement()
{
    m_is_thread_running = false;

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

const TempMeasurementData TemperatureMeasurement::get_data()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_data;
}