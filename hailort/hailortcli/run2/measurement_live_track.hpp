/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
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

#include "live_printer.hpp"


class MeasurementLiveTrack : public LivePrinter::Track
{
public:
    static hailort::Expected<std::shared_ptr<MeasurementLiveTrack>> create_shared(hailort::Device &vdevice, bool measure_power,
        bool measure_current, bool measure_temp);

    virtual ~MeasurementLiveTrack() = default;
    virtual hailo_status start() override;
    virtual uint32_t get_text(std::stringstream &ss) override;

    MeasurementLiveTrack(std::shared_ptr<PowerMeasurement> power_measurement, std::shared_ptr<PowerMeasurement> current_measurement,
        std::shared_ptr<TemperatureMeasurement> temp_measurement, const std::string &device_id);

private:
    std::shared_ptr<PowerMeasurement> m_power_measurement;
    std::shared_ptr<PowerMeasurement> m_current_measurement;
    std::shared_ptr<TemperatureMeasurement> m_temp_measurement;
    
    std::string m_device_id;
};

#endif /* _HAILO_HAILORTCLI_RUN2_MEASUREMENT_LIVE_TRACK_HPP_ */