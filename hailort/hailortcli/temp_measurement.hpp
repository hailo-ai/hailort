/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file temp_measurement.hpp
 * @brief Measure temperature of Hailo chip
 **/

#ifndef _HAILO_TEMP_MEASUREMENT_HPP_
#define _HAILO_TEMP_MEASUREMENT_HPP_

#include "hailortcli.hpp"
#include "command.hpp"
#include "hailo/hailort.h"
#include "hailo/device.hpp"
#include "CLI/CLI.hpp"

#include <thread>

struct TempMeasurementData {
    float32_t average_value;
    float32_t min_value;
    float32_t max_value;
    uint32_t sample_count;
};


class TemperatureMeasurement final {
public:
    TemperatureMeasurement(Device &device);
    virtual ~TemperatureMeasurement();

    hailo_status start_measurement();
    void stop_measurement();
    const TempMeasurementData get_data();

private:
    void measure_temp();

    Device &m_device;
    std::thread m_thread;
    std::atomic_bool m_is_thread_running;
    std::mutex m_mutex;
    TempMeasurementData m_data;
};

#endif /* _HAILO_TEMP_MEASUREMENT_HPP_ */
