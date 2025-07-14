/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file runtime_statistics.hpp
 * @brief Runtime Statistics
 **/

#ifndef _HAILO_RUNTIME_STATISTICS_HPP_
#define _HAILO_RUNTIME_STATISTICS_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include <type_traits>
#include <memory>

/** hailort namespace */
namespace hailort
{

/*! Results obtained by an Accumulator at a given point in time via Accumulator::get_and_clear or Accumulator::get */
class AccumulatorResults final
{
public:
    AccumulatorResults(const Expected<size_t> &count, const Expected<double> &min, const Expected<double> &max,
                       const Expected<double> &mean, const Expected<double> &var, const Expected<double> &sd,
                       const Expected<double> &mean_sd) :
        m_count(count),
        m_min(min),
        m_max(max),
        m_mean(mean),
        m_var(var),
        m_sd(sd),
        m_mean_sd(mean_sd)
    {}

    AccumulatorResults(AccumulatorResults &&) = default;
    AccumulatorResults(const AccumulatorResults &) = delete;
    AccumulatorResults &operator=(AccumulatorResults &&) = delete;
    AccumulatorResults &operator=(const AccumulatorResults &) = delete;
    ~AccumulatorResults() = default;

    /**
     * @return Returns Expected of the number of datapoints added to the Accumulator.
     */
    Expected<size_t> count() const { return m_count; }

    /**
     * @return Returns Expected of the minimal value added to the Accumulator,
     *         or Unexpected of ::HAILO_UNINITIALIZED if no data has been added.
     */
    Expected<double> min() const { return m_min; }

    /**
     * @return Returns Expected of the maximal value added to the Accumulator,
     *         or Unexpected of ::HAILO_UNINITIALIZED if no data has been added.
     */
    Expected<double> max() const { return m_max; }

    /**
     * @return Returns Expected of the mean of the values added to the Accumulator,
     *         or Unexpected of ::HAILO_UNINITIALIZED if no data has been added.
     */
    Expected<double> mean() const { return m_mean; }

    /**
     * @return Returns Expected of the sample variance of the values added to the Accumulator,
     *         or Unexpected of ::HAILO_UNINITIALIZED if no data has been added.
     */
    Expected<double> var() const { return m_var; }

    /**
     * @return Returns Expected of the sample standard deviation of the values added to the Accumulator,
     *         or Unexpected of ::HAILO_UNINITIALIZED if no data has been added.
     */
    Expected<double> sd() const { return m_sd; }

    /**
     * @return Returns Expected of the sample standard deviation of the mean of values added to the Accumulator,
     *         or Unexpected of ::HAILO_UNINITIALIZED if no data has been added.
     */
    Expected<double> mean_sd() const { return m_mean_sd; }

private:
    const Expected<size_t> m_count;
    const Expected<double> m_min;
    const Expected<double> m_max;
    const Expected<double> m_mean;
    const Expected<double> m_var;
    const Expected<double> m_sd;
    const Expected<double> m_mean_sd;
};

/*! The Accumulator interface supports the measurement of various statistics incrementally. I.e. upon each addition of
    a measurement to the Accumulator, via Accumulator::add_data_point, all the statistics are updated.
    Implementations of this interface are to be thread-safe, meaning that adding measurements in one thread, while another
    thread reads the current statistics (via the various getters provided by the interface) will produce correct values.
*/
template<typename T, std::enable_if_t<std::is_arithmetic<T>::value, int> = 0>
class Accumulator
{
public:
    /**
     * Constructs a new Accumulator with a given @a data_type
     *
     * @param data_type             The type of data that will be measured by the Accumulator.
     *                              Used to differentiate between different types of measurements (e.g. fps, latency).
     */
    Accumulator(const std::string& data_type) :
        m_data_type(data_type)
    {}

    Accumulator(Accumulator &&) = default;
    Accumulator(const Accumulator &) = delete;
    Accumulator &operator=(Accumulator &&) = delete;
    Accumulator &operator=(const Accumulator &) = delete;
    virtual ~Accumulator() = default;

    /**
     * @return The data_type of the Accumulator.
     */
    std::string get_data_type() const { return m_data_type; };

    /**
     * Add a new measurement to the Accumulator, updating the statistics measured.
     * 
     * @param data                  The measurement to be added.
     * @param samples_count         The weight of the measurement to be considered in average calculations.
     * @note Implementations of this interface are to update the statistics in constant time
     */
    virtual void add_data_point(T data, uint32_t samples_count = 1) = 0;

    /**
     * Gets the current statistics of the data added to the Accumulator, clearing the statistics afterwards.
     * 
     * @return The current statistics of the data added to the Accumulator
     */
    virtual AccumulatorResults get_and_clear() = 0;
    
    /**
     * @return The current statistics of the data added to the Accumulator
     */
    virtual AccumulatorResults get() const = 0;
    
    /**
     * @return The number of datapoints added to the Accumulator.
     */
    virtual Expected<size_t> count() const = 0;
    
    /**
     * @return Returns Expected of the minimal value added to the Accumulator,
     *         or Unexpected of ::HAILO_UNINITIALIZED if no data has been added.
     */
    virtual Expected<double> min() const = 0;
    
    /**
     * @return Returns Expected of the maximal value added to the Accumulator,
     *         or Unexpected of ::HAILO_UNINITIALIZED if no data has been added.
     */
    virtual Expected<double> max() const = 0;
    
    /**
     * @return Returns Expected of the mean of the values added to the Accumulator,
     *         or Unexpected of ::HAILO_UNINITIALIZED if no data has been added.
     */
    virtual Expected<double> mean() const = 0;
    
    /**
     * @return Returns Expected of the sample variance of the values added to the Accumulator,
     *         or Unexpected of ::HAILO_UNINITIALIZED if no data has been added.
     */
    virtual Expected<double> var() const = 0;
    
    /**
     * @return Returns Expected of the sample standard deviation of the values added to the Accumulator,
     *         or Unexpected of ::HAILO_UNINITIALIZED if no data has been added.
     */
    virtual Expected<double> sd() const = 0;

    /**
     * @return Returns Expected of the sample standard deviation of the mean of values added to the Accumulator,
     *         or Unexpected of ::HAILO_UNINITIALIZED if no data has been added.
     */
    virtual Expected<double> mean_sd() const = 0;

private:
    const std::string m_data_type;
};
using AccumulatorPtr = std::shared_ptr<Accumulator<double>>;

} /* namespace hailort */

#endif /* _HAILO_RUNTIME_STATISTICS_HPP_ */
