/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file device_measurements.hpp
 * @brief Measure temperature, power and current of Hailo chip
 **/

#ifndef _HAILO_DEVICE_MEASUREMENTS_HPP_
#define _HAILO_DEVICE_MEASUREMENTS_HPP_

#include "hailo/hailort.h"
#include "hailo/device.hpp"

#include "common/runtime_statistics_internal.hpp"

#include <thread>
#include <mutex>
#include <atomic>


enum class ShouldMeasurePower {
    AUTO_DETECT, // auto detect if should measure power, based on device.get_capabilities()
    NO,
    YES
};

class BaseMeasurement
{
public:
    BaseMeasurement(hailort::Device &device, hailo_status &status);
    virtual ~BaseMeasurement();

    virtual hailo_status start_measurement() = 0;
    void stop_measurement();
    hailort::AccumulatorResults get_data();

    virtual std::string measurement_unit() = 0;

protected:
    hailort::Device &m_device;
    std::thread m_thread;
    std::atomic_bool m_is_thread_running;
    std::mutex m_mutex;
    hailort::AccumulatorPtr m_acc;

private:
    virtual hailo_status sanity_check() = 0;
};


class TemperatureMeasurement : public BaseMeasurement
{
public:
    static hailort::Expected<std::shared_ptr<TemperatureMeasurement>> create_shared(hailort::Device &device);

    virtual ~TemperatureMeasurement() = default;

    virtual hailo_status start_measurement() override;

    virtual std::string measurement_unit() override
    {
        return "C";
    }

    TemperatureMeasurement(hailort::Device &device, hailo_status &status);

private:
    virtual hailo_status sanity_check() override;
};


class PowerMeasurement : public BaseMeasurement
{
public:
    static hailort::Expected<std::shared_ptr<PowerMeasurement>> create_shared(hailort::Device &device,
        hailo_power_measurement_types_t measurement_type);
    virtual ~PowerMeasurement() = default;

    virtual hailo_status start_measurement() override;

    virtual std::string measurement_unit() override
    {
        switch (m_measurement_type) {
        case HAILO_POWER_MEASUREMENT_TYPES__SHUNT_VOLTAGE:
        case HAILO_POWER_MEASUREMENT_TYPES__BUS_VOLTAGE:
            return "mV";
        case HAILO_POWER_MEASUREMENT_TYPES__AUTO:
        case HAILO_POWER_MEASUREMENT_TYPES__POWER:
            return "W";
        case HAILO_POWER_MEASUREMENT_TYPES__CURRENT:
            return "mA";
        default:
            return "Nan";
        };
    }

    PowerMeasurement(hailort::Device &device, hailo_power_measurement_types_t measurement_type,
        hailo_status &status);

private:
    hailo_power_measurement_types_t m_measurement_type;
    virtual hailo_status sanity_check() override;
};

#endif /* _HAILO_DEVICE_MEASUREMENTS_HPP_ */
