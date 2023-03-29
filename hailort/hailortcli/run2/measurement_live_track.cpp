/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file measurement_live_track.cpp
 * @brief Device measurements live track
 **/

#include "hailo/hailort.h"

#include "common/device_measurements.hpp"
#include "common/utils.hpp"

#include "measurement_live_track.hpp"

#include <spdlog/fmt/fmt.h>
#include <sstream>


using namespace hailort;

Expected<std::shared_ptr<MeasurementLiveTrack>> MeasurementLiveTrack::create_shared(Device &device, bool measure_power, bool measure_current,
    bool measure_temp)
{
    std::shared_ptr<PowerMeasurement> power_measurement = nullptr;
    if (measure_power) {
        auto power_measurement_exp = PowerMeasurement::create_shared(device, HAILO_POWER_MEASUREMENT_TYPES__POWER);
        CHECK_EXPECTED(power_measurement_exp);
        power_measurement = power_measurement_exp.release();
    }

    std::shared_ptr<PowerMeasurement> current_measurement = nullptr;
    if (measure_current) {
        auto current_measurement_exp = PowerMeasurement::create_shared(device, HAILO_POWER_MEASUREMENT_TYPES__CURRENT);
        CHECK_EXPECTED(current_measurement_exp);
        current_measurement = current_measurement_exp.release();
    }

    std::shared_ptr<TemperatureMeasurement> temp_measurement = nullptr;
    if (measure_temp) {
        auto temp_measurement_exp = TemperatureMeasurement::create_shared(device);
        CHECK_EXPECTED(temp_measurement_exp);
        temp_measurement = temp_measurement_exp.release();
    }

    auto ptr = make_shared_nothrow<MeasurementLiveTrack>(power_measurement, current_measurement, temp_measurement, device.get_dev_id());
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return ptr;
}

MeasurementLiveTrack::MeasurementLiveTrack(std::shared_ptr<PowerMeasurement> power_measurement,
    std::shared_ptr<PowerMeasurement> current_measurement, std::shared_ptr<TemperatureMeasurement> temp_measurement,
    const std::string &device_id) :
        LivePrinter::Track(), m_power_measurement(std::move(power_measurement)), m_current_measurement(std::move(current_measurement)),
        m_temp_measurement(std::move(temp_measurement)), m_device_id(device_id)
{}

hailo_status MeasurementLiveTrack::start()
{
    if (m_power_measurement) {
        CHECK_SUCCESS(m_power_measurement->start_measurement());
    }

    if (m_current_measurement) {
        CHECK_SUCCESS(m_current_measurement->start_measurement());
    }

    if (m_temp_measurement) {
        CHECK_SUCCESS(m_temp_measurement->start_measurement());
    }

    m_started = true;

    return HAILO_SUCCESS;
}

uint32_t MeasurementLiveTrack::get_text(std::stringstream &ss)
{
    if (!m_started) {
        return 0;
    }

    auto rows_count = 0;

    if (m_power_measurement || m_current_measurement || m_temp_measurement) {
        ss << fmt::format("\nMeasurements for device {}\n", m_device_id);
        rows_count += 2;
    }

    if (m_power_measurement) {
        auto measurement_info = m_power_measurement->get_data();
        if (auto min = measurement_info.min()) {
            ss << fmt::format("\tMinimum power consumption: {:.2f} {}\n", *min, m_power_measurement->measurement_unit());
            rows_count++;
        }
        if (auto mean = measurement_info.mean()) {
            ss << fmt::format("\tAverage power consumption: {:.2f} {}\n", *mean, m_power_measurement->measurement_unit());
            rows_count++;
        }
        if (auto max = measurement_info.max()) {
            ss << fmt::format("\tMaximum power consumption: {:.2f} {}\n", *max, m_power_measurement->measurement_unit());
            rows_count++;
        }
    }

    if (m_current_measurement) {
        auto measurement_info = m_current_measurement->get_data();
        if (auto min = measurement_info.min()) {
            ss << fmt::format("\tMinimum current consumption: {:.2f} {}\n", *min, m_current_measurement->measurement_unit());
            rows_count++;
        }
        if (auto mean = measurement_info.mean()) {
            ss << fmt::format("\tAverage current consumption: {:.2f} {}\n", *mean, m_current_measurement->measurement_unit());
            rows_count++;
        }
        if (auto max = measurement_info.max()) {
            ss << fmt::format("\tMaximum current consumption: {:.2f} {}\n", *max, m_current_measurement->measurement_unit());
            rows_count++;
        }
    }

    if (m_temp_measurement) {
        auto measurement_info = m_temp_measurement->get_data();
        if (auto min = measurement_info.min()) {
            ss << fmt::format("\tMinimum chip temperature: {:.2f} {}\n", *min, m_temp_measurement->measurement_unit());
            rows_count++;
        }
        if (auto mean = measurement_info.mean()) {
            ss << fmt::format("\tAverage chip temperature: {:.2f} {}\n", *mean, m_temp_measurement->measurement_unit());
            rows_count++;
        }
        if (auto max = measurement_info.max()) {
            ss << fmt::format("\tMaximum chip temperature: {:.2f} {}\n", *max, m_temp_measurement->measurement_unit());
            rows_count++;
        }
    }

    return rows_count;
}