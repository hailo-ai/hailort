/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file measurement_utils_internal.hpp
 * @brief Internal class definitions for the measurement_utils module
 **/

#ifndef _HAILO_MEASUREMENT_UTILS_INTERNAL_HPP_
#define _HAILO_MEASUREMENT_UTILS_INTERNAL_HPP_

#include "hailo/hailort.h"
#include "utils/hailort_logger.hpp"
#include "common/runtime_statistics_internal.hpp"

#include <stdlib.h>
#include <chrono>
#include <iostream>
#include <sstream>


namespace hailort {
namespace utils {

enum class MeasurementType
{
    TIME,
    FPS,
    VALUE
};

// Interface used for dependency injection
class ITimeProvider
{
public:
    using time_point = decltype(std::chrono::high_resolution_clock::now());
    virtual time_point now() const = 0;
    virtual ~ITimeProvider() = default;
};

// Note: This class is a singleton, to avoid the overhead of creating multiple instances
//       (it's got no state so there's no reason to have multiple instances)
class HighResTimeProvider : public ITimeProvider
{
public:
    static const ITimeProvider& get_instance();
    virtual time_point now() const override;
};

class MeasurementStorage final
{
public:
    // Adds a 'type' measurement to the 'accumulator_name' accumulator; thread-safe
    static hailo_status add_measurement(const std::string &accumulator_name, MeasurementType type, double measurement);
    static Expected<AccumulatorResults> get_measurements(MeasurementType type, const std::string &accumulator_name);
    // Not thread-safe
    static void set_verbosity(bool verbosity);
    static void set_precision(uint32_t precision);
    static void clear();
    static void show_output_on_destruction(bool show_output);

    ~MeasurementStorage();

private:
    struct AccumulatorMap {
        std::mutex mutex;
        std::unordered_map<std::string, AccumulatorPtr> map;
    };

    static MeasurementStorage& get_instance();
    static std::string indent_string(const std::string &str, uint8_t indent_level);

    AccumulatorMap &get_storage(MeasurementType type);
    std::vector<std::pair<std::string, AccumulatorPtr>> get_sorted_elements(MeasurementType type);
    std::string get_measurement_title(MeasurementType type);
    void format_measurements(std::ostream &output_stream, MeasurementType type);
    hailo_status add_measurement_impl(const std::string &accumulator_name, MeasurementType type, double measurement);
    Expected<AccumulatorResults> get_measurements_impl(MeasurementType type, const std::string &accumulator_name);
    void set_verbosity_impl(bool verbosity);
    void set_precision_impl(uint32_t precision);
    void clear_impl();
    void show_output_on_destruction_impl(bool show_output);

    bool m_verbose = false;
    uint32_t m_precision = AccumulatorResultsHelper::DEFAULT_FLOATING_POINT_PRECISION;
    bool m_show_output_on_destruction = true;
    AccumulatorMap m_time_acc_storage;
    AccumulatorMap m_fps_acc_storage;
    AccumulatorMap m_value_acc_storage;
};

class Measure
{
public:
    virtual ~Measure()
    {
        const auto status = MeasurementStorage::add_measurement(m_accumulator_name, m_type, m_measurement);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed adding data point to {}", m_accumulator_name);
        }
    }

    Measure(Measure &&) = delete;
    Measure(const Measure &) = delete;
    Measure &operator=(Measure &&) = delete;
    Measure &operator=(const Measure &) = delete;

protected:
    // The measurement will be added to the accumulator named m_accumulator_name in the dtor
    double m_measurement;

    Measure(MeasurementType type, const std::string &accumulator_name) :
        m_measurement(),
        m_type(type),
        m_accumulator_name(accumulator_name)
    {}

private:
    const MeasurementType m_type;
    const std::string m_accumulator_name;
};

template <typename RatioType>
class MeasureTimeBase : public Measure
{
public:
    virtual ~MeasureTimeBase()
    {
        // Set the measurement to the time delta
        m_measurement = convert_to_double(m_time_provider.now() - m_start_time);
    }

protected:
    MeasureTimeBase(MeasurementType type, const std::string &accumulator_name, const ITimeProvider& time_provider) :
        Measure::Measure(type, accumulator_name),
        m_time_provider(time_provider),
        m_start_time(m_time_provider.now())
    {}

private:
    static double convert_to_double(std::chrono::nanoseconds time_in_ns)
    {
        return std::chrono::duration<double, RatioType>(time_in_ns).count();
    }

    const ITimeProvider &m_time_provider;
    // Must be the last member declared, so that the time will be measured correctly
    const ITimeProvider::time_point m_start_time;
};

} /* namespace utils */
} /* namespace hailort */

#endif /* _HAILO_MEASUREMENT_UTILS_INTERNAL_HPP_ */
