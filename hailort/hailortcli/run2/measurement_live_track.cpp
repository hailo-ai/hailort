/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
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

using namespace hailort;

Expected<std::shared_ptr<MeasurementLiveTrack>> MeasurementLiveTrack::create_shared(const std::string &device_id,
    bool measure_power, bool measure_current, bool measure_temp)
{
    // TODO HRT-20327: Current Device API allows only 1 concurrent measurement per device. As a workaround we open a
    // new device per measurement that we want to make.
    std::vector<std::unique_ptr<hailort::Device>> device_gaurd;

    std::shared_ptr<PowerMeasurement> power_measurement = nullptr;
    if (measure_power) {
        TRY(auto device, Device::create(device_id));
        TRY(power_measurement, PowerMeasurement::create_shared(*device, HAILO_POWER_MEASUREMENT_TYPES__POWER));
        device_gaurd.push_back(std::move(device));
    }

    std::shared_ptr<PowerMeasurement> current_measurement = nullptr;
    if (measure_current) {
        TRY(auto device, Device::create(device_id));
        TRY(current_measurement, PowerMeasurement::create_shared(*device, HAILO_POWER_MEASUREMENT_TYPES__CURRENT));
        device_gaurd.push_back(std::move(device));
    }

    std::shared_ptr<TemperatureMeasurement> temp_measurement = nullptr;
    if (measure_temp) {
        TRY(auto device, Device::create(device_id));
        TRY(temp_measurement, TemperatureMeasurement::create_shared(*device));
        device_gaurd.push_back(std::move(device));
    }

    auto ptr = make_shared_nothrow<MeasurementLiveTrack>(
        power_measurement, current_measurement, temp_measurement, device_id, std::move(device_gaurd));
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return ptr;
}

MeasurementLiveTrack::MeasurementLiveTrack(std::shared_ptr<PowerMeasurement> power_measurement,
    std::shared_ptr<PowerMeasurement> current_measurement, std::shared_ptr<TemperatureMeasurement> temp_measurement,
    const std::string &device_id, std::vector<std::unique_ptr<hailort::Device>> &&device_guard)
    : LiveStats::Track(),
      m_device_guard(std::move(device_guard)), // NOTE: must be first so that dtor is called last.
      m_power_measurement(std::move(power_measurement)),
      m_current_measurement(std::move(current_measurement)),
      m_temp_measurement(std::move(temp_measurement)),
      m_device_id(device_id)
{}

hailo_status MeasurementLiveTrack::start_impl()
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

    return HAILO_SUCCESS;
}

std::string MeasurementLiveTrack::get_text_impl() const
{
    std::string s;
    if (m_power_measurement || m_current_measurement || m_temp_measurement) {
        s += fmt::format("\nMeasurements for device {}\n", m_device_id);
    }

    if (m_power_measurement) {
        auto measurement_info = m_power_measurement->get_data();
        if (auto min = measurement_info.min()) {
            s += fmt::format("\tMinimum power consumption: {:.2f} {}\n", *min, m_power_measurement->measurement_unit());
        }
        if (auto mean = measurement_info.mean()) {
            s += fmt::format("\tAverage power consumption: {:.2f} {}\n", *mean, m_power_measurement->measurement_unit());
        }
        if (auto max = measurement_info.max()) {
            s += fmt::format("\tMaximum power consumption: {:.2f} {}\n", *max, m_power_measurement->measurement_unit());
        }
    }

    if (m_current_measurement) {
        auto measurement_info = m_current_measurement->get_data();
        if (auto min = measurement_info.min()) {
            s += fmt::format("\tMinimum current consumption: {:.2f} {}\n", *min, m_current_measurement->measurement_unit());
        }
        if (auto mean = measurement_info.mean()) {
            s += fmt::format("\tAverage current consumption: {:.2f} {}\n", *mean, m_current_measurement->measurement_unit());
        }
        if (auto max = measurement_info.max()) {
            s += fmt::format("\tMaximum current consumption: {:.2f} {}\n", *max, m_current_measurement->measurement_unit());
        }
    }

    if (m_temp_measurement) {
        auto measurement_info = m_temp_measurement->get_data();
        if (auto min = measurement_info.min()) {
            s += fmt::format("\tMinimum chip temperature: {:.2f} {}\n", *min, m_temp_measurement->measurement_unit());
        }
        if (auto mean = measurement_info.mean()) {
            s += fmt::format("\tAverage chip temperature: {:.2f} {}\n", *mean, m_temp_measurement->measurement_unit());
        }
        if (auto max = measurement_info.max()) {
            s += fmt::format("\tMaximum chip temperature: {:.2f} {}\n", *max, m_temp_measurement->measurement_unit());
        }
    }

    return s;
}

void MeasurementLiveTrack::push_json_measurment_val(nlohmann::ordered_json &device_json, std::shared_ptr<BaseMeasurement> measurment, const std::string &measurment_name)
{
    auto measurment_info = measurment->get_data();
    auto measurement_unit = measurment->measurement_unit();
    auto min = measurment_info.min();
    auto max = measurment_info.max();
    auto mean = measurment_info.mean();
    if (min && max && mean){
        device_json[measurment_name] = {
            {"min", std::to_string(min.value()) + " " + measurement_unit},
            {"max", std::to_string(max.value()) + " " + measurement_unit},
            {"average", std::to_string(mean.value()) + " " + measurement_unit}
        };
    }
}

void MeasurementLiveTrack::push_json_impl(nlohmann::ordered_json &json)
{
    nlohmann::ordered_json device_json;
    device_json["device_id"] = m_device_id;

    if (m_power_measurement){
        push_json_measurment_val(device_json, m_power_measurement, "power");
    }
    if (m_current_measurement){
        push_json_measurment_val(device_json, m_current_measurement, "current");
    }
    if (m_temp_measurement){
        push_json_measurment_val(device_json, m_temp_measurement, "temperature");
    }
    json["devices"].emplace_back(device_json);
}
