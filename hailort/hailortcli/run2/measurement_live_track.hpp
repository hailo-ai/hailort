/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file measurement_live_track.hpp
 * @brief Device measurements live track
 **/

#ifndef _HAILO_HAILORTCLI_RUN2_MEASUREMENT_LIVE_TRACK_HPP_
#define _HAILO_HAILORTCLI_RUN2_MEASUREMENT_LIVE_TRACK_HPP_

#include "hailo/hailort.h"

#include "common/device_measurements.hpp"
#include "live_stats.hpp"

#include <nlohmann/json.hpp>

class MeasurementLiveTrack : public LiveStats::Track
{
public:
    static hailort::Expected<std::shared_ptr<MeasurementLiveTrack>> create_shared(hailort::Device &vdevice, bool measure_power,
        bool measure_current, bool measure_temp);

    virtual ~MeasurementLiveTrack() = default;
    virtual hailo_status start_impl() override;
    virtual uint32_t push_text_impl(std::stringstream &ss) override;
    virtual void push_json_impl(nlohmann::ordered_json &json) override;

    MeasurementLiveTrack(std::shared_ptr<PowerMeasurement> power_measurement, std::shared_ptr<PowerMeasurement> current_measurement,
        std::shared_ptr<TemperatureMeasurement> temp_measurement, const std::string &device_id);

    std::shared_ptr<PowerMeasurement> get_power_measurement() { return m_power_measurement; }
    std::shared_ptr<PowerMeasurement> get_current_measurement() { return m_current_measurement; }
    std::shared_ptr<TemperatureMeasurement> get_temp_measurement() { return m_temp_measurement; }
    const std::string &get_device_id() const { return m_device_id; }

private:
    void push_json_measurment_val(nlohmann::ordered_json &device_json, std::shared_ptr<BaseMeasurement> measurment, const std::string &measurment_name);
    std::shared_ptr<PowerMeasurement> m_power_measurement;
    std::shared_ptr<PowerMeasurement> m_current_measurement;
    std::shared_ptr<TemperatureMeasurement> m_temp_measurement;

    std::string m_device_id;
};

#endif /* _HAILO_HAILORTCLI_RUN2_MEASUREMENT_LIVE_TRACK_HPP_ */
