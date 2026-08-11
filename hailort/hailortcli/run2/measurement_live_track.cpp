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

#include <string>
#include <string_view>

using namespace hailort;

static constexpr size_t METRIC_LABEL_WIDTH = 16;
static constexpr size_t METRIC_VALUE_WIDTH = 10;

namespace {

const std::string NA_PADDED = fmt::format("{:<{}}", "N/A", METRIC_VALUE_WIDTH);

std::string format_padded_value(double value, std::string_view unit)
{
    return fmt::format("{:<{}}", fmt::format("{:.2f} {}", value, unit), METRIC_VALUE_WIDTH);
}

} // namespace

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
      m_device_id(device_id),
      m_last_interval_power_mean(0.0),
      m_last_interval_current_mean(0.0),
      m_last_interval_temp_mean(0.0)
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

void MeasurementLiveTrack::measure()
{
    auto get_interval_mean = [] (BaseMeasurement &measurement) -> double {
        auto interval_info = measurement.get_interval_data_and_reset();
        if (auto mean = interval_info.mean()) {
            return *mean;
        }
        return 0.0;
    };

    if (m_power_measurement) {
        m_last_interval_power_mean = get_interval_mean(*m_power_measurement);
    }
    if (m_current_measurement) {
        m_last_interval_current_mean = get_interval_mean(*m_current_measurement);
    }
    if (m_temp_measurement) {
        m_last_interval_temp_mean = get_interval_mean(*m_temp_measurement);
    }
}

std::string MeasurementLiveTrack::get_text_impl() const
{
    if (!m_power_measurement && !m_current_measurement && !m_temp_measurement) {
        return "";
    }

    std::string s = "\n";
    s += fmt::format("{:<{}} | {:<{}} | {:<{}} | {:<{}} | {:<{}}\n",
        "", METRIC_LABEL_WIDTH,
        "cur", METRIC_VALUE_WIDTH,
        "min", METRIC_VALUE_WIDTH,
        "avg", METRIC_VALUE_WIDTH,
        "max", METRIC_VALUE_WIDTH);

    auto append_row = [&s] (const std::string &label, BaseMeasurement &measurement, double interval_mean) {
        const auto unit = measurement.measurement_unit();
        const auto info = measurement.get_data();
        const auto cur_str = format_padded_value(interval_mean, unit);
        const auto min_str = info.min() ? format_padded_value(*info.min(), unit) : NA_PADDED;
        const auto avg_str = info.mean() ? format_padded_value(*info.mean(), unit) : NA_PADDED;
        const auto max_str = info.max() ? format_padded_value(*info.max(), unit) : NA_PADDED;
        s += fmt::format("{:<{}} | {} | {} | {} | {}\n",
            label, METRIC_LABEL_WIDTH, cur_str, min_str, avg_str, max_str);
    };

    if (m_power_measurement) {
        append_row("Power", *m_power_measurement, m_last_interval_power_mean);
    }
    if (m_current_measurement) {
        append_row("Current", *m_current_measurement, m_last_interval_current_mean);
    }
    if (m_temp_measurement) {
        append_row("Chip temperature", *m_temp_measurement, m_last_interval_temp_mean);
    }

    return s;
}

std::string MeasurementLiveTrack::get_summary_text_impl() const
{
    if (!m_power_measurement && !m_current_measurement && !m_temp_measurement) {
        return "";
    }

    std::string s = "\n";
    s += fmt::format("{:<{}} | {:<{}} | {:<{}} | {:<{}}\n",
        "", METRIC_LABEL_WIDTH,
        "min", METRIC_VALUE_WIDTH,
        "avg", METRIC_VALUE_WIDTH,
        "max", METRIC_VALUE_WIDTH);

    auto append_row = [&s] (const std::string &label, BaseMeasurement &measurement) {
        const auto unit = measurement.measurement_unit();
        const auto info = measurement.get_data();
        const auto min_str = info.min() ? format_padded_value(*info.min(), unit) : NA_PADDED;
        const auto avg_str = info.mean() ? format_padded_value(*info.mean(), unit) : NA_PADDED;
        const auto max_str = info.max() ? format_padded_value(*info.max(), unit) : NA_PADDED;
        s += fmt::format("{:<{}} | {} | {} | {}\n",
            label, METRIC_LABEL_WIDTH, min_str, avg_str, max_str);
    };

    if (m_power_measurement) {
        append_row("Power", *m_power_measurement);
    }
    if (m_current_measurement) {
        append_row("Current", *m_current_measurement);
    }
    if (m_temp_measurement) {
        append_row("Chip temperature", *m_temp_measurement);
    }

    return s;
}

void MeasurementLiveTrack::push_json_measurment_val(nlohmann::ordered_json &device_json,
    std::shared_ptr<BaseMeasurement> measurment, const std::string &measurment_name)
{
    const auto info = measurment->get_data();
    const auto measurement_unit = measurment->measurement_unit();
    const auto min = info.min();
    const auto max = info.max();
    const auto mean = info.mean();
    if (min && max && mean) {
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
