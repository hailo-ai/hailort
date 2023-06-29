/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file device_measurements.hpp
 * @brief Measure temperature, power and current of Hailo chip
 **/


#include "common/device_measurements.hpp"
#include "common/utils.hpp"

using namespace hailort;

constexpr std::chrono::milliseconds DEFAULT_MEASUREMENTS_INTERVAL(100);

BaseMeasurement::BaseMeasurement(Device &device, hailo_status &status) :
    m_device(device),
    m_is_thread_running(false),
    m_acc(make_shared_nothrow<FullAccumulator<double>>("BaseMeasurementAccumulator"))
{
    if (nullptr == m_acc) {
        status = HAILO_OUT_OF_HOST_MEMORY;
        return;
    }
    status = HAILO_SUCCESS;
}

BaseMeasurement::~BaseMeasurement()
{
    stop_measurement();
}

void BaseMeasurement::stop_measurement()
{
    m_is_thread_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

AccumulatorResults BaseMeasurement::get_data()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_acc->get();
}

Expected<std::shared_ptr<TemperatureMeasurement>> TemperatureMeasurement::create_shared(Device &device)
{
    auto status = HAILO_UNINITIALIZED;
    auto ptr = make_shared_nothrow<TemperatureMeasurement>(device, status);
    CHECK_SUCCESS_AS_EXPECTED(status);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return ptr;
}

TemperatureMeasurement::TemperatureMeasurement(Device &device, hailo_status &status) : BaseMeasurement(device, status)
{
    /* Executing the check only if BaseMeasurement constructor has succeeded */
    if (HAILO_SUCCESS == status) {
        status = sanity_check();
    }
}

hailo_status TemperatureMeasurement::sanity_check()
{
    auto temp_measurement = m_device.get_chip_temperature();
    return temp_measurement.status();
}

hailo_status TemperatureMeasurement::start_measurement()
{
    m_is_thread_running = true;
    m_thread = std::thread([this] () {
        while (m_is_thread_running.load()) {
            auto temp_info = m_device.get_chip_temperature();
            if (HAILO_SUCCESS != temp_info.status()) {
                LOGGER__ERROR("Failed to get chip's temperature, status = {}", temp_info.status());
                m_is_thread_running = false;
                break;
            }

            float32_t ts_avg = ((temp_info->ts0_temperature + temp_info->ts1_temperature) / 2);
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_acc->add_data_point(ts_avg, temp_info->sample_count);
            }
            
            std::this_thread::sleep_for(DEFAULT_MEASUREMENTS_INTERVAL); 
        }
    });

    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<PowerMeasurement>> PowerMeasurement::create_shared(Device &device,
    hailo_power_measurement_types_t measurement_type)
{
    auto status = HAILO_UNINITIALIZED;
    auto ptr = make_shared_nothrow<PowerMeasurement>(device, measurement_type, status);
    CHECK_SUCCESS_AS_EXPECTED(status);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return ptr;
}

PowerMeasurement::PowerMeasurement(Device &device, hailo_power_measurement_types_t measurement_type, hailo_status &status)
    : BaseMeasurement(device, status), m_measurement_type(measurement_type)
{
    /* Executing the check only if BaseMeasurement constructor has succeeded */
    if (HAILO_SUCCESS == status) {
        status = sanity_check();
    }
}

hailo_status PowerMeasurement::sanity_check()
{
    auto power_measurement = m_device.power_measurement(HAILO_DVM_OPTIONS_AUTO, m_measurement_type);
    return power_measurement.status();
}

hailo_status PowerMeasurement::start_measurement()
{
    m_is_thread_running = true;
    m_thread = std::thread([this] () {
        while (m_is_thread_running.load()) {
            auto power_info = m_device.power_measurement(HAILO_DVM_OPTIONS_AUTO, m_measurement_type);
            if (HAILO_SUCCESS != power_info.status()) {
                LOGGER__ERROR("Failed to get chip's power, status = {}", power_info.status());
                m_is_thread_running = false;
                break;
            }

            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_acc->add_data_point(*power_info);
            }
            
            std::this_thread::sleep_for(DEFAULT_MEASUREMENTS_INTERVAL); 
        }
    });

    return HAILO_SUCCESS;
}