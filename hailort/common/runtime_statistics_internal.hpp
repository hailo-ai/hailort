/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file runtime_statistics_internal.hpp
 * @brief Implementation of Accumulator<T> interface
 **/

#ifndef _HAILO_RUNTIME_STATISTICS_INTERNAL_HPP_
#define _HAILO_RUNTIME_STATISTICS_INTERNAL_HPP_

#include "hailo/runtime_statistics.hpp"

#include <cmath>
#include <mutex>
#include <limits>
#include <ostream>
#include <sstream>
#include <iomanip>

namespace hailort
{

class AccumulatorResultsHelper final
{
public:
    AccumulatorResultsHelper() = delete;

    static const uint32_t DEFAULT_FLOATING_POINT_PRECISION = 4;

    static std::string format_results(const AccumulatorResults &results, bool verbose = false,
        uint32_t precision = DEFAULT_FLOATING_POINT_PRECISION)
    {
        std::stringstream stream;
        stream << format_statistic(results.count(), "count") << ", ";
        stream << format_statistic(results.mean(), "mean", precision);
        if (verbose) {
            stream << ", ";
            stream << format_statistic(results.min(), "min", precision) << ", ";
            stream << format_statistic(results.max(), "max", precision) << ", ";
            stream << format_statistic(results.var(), "var", precision) << ", ";
            stream << format_statistic(results.sd(), "sd", precision) << ", ";
            stream << format_statistic(results.mean_sd(), "mean_sd", precision);
        }
        return stream.str();
    }

    static std::string format_statistic(const Expected<double> &statistic, const std::string &name = "",
        uint32_t precision = DEFAULT_FLOATING_POINT_PRECISION)
    {
        return format_statistic<double>(statistic, name, precision);
    }

    static std::string format_statistic(const Expected<size_t> &statistic, const std::string &name = "")
    {
        return format_statistic<size_t>(statistic, name);
    }

private:
    template<typename T, std::enable_if_t<std::is_arithmetic<T>::value, int> = 0>
    static std::string format_statistic(const Expected<T> &statistic, const std::string &name,
        uint32_t precision = DEFAULT_FLOATING_POINT_PRECISION)
    {
        static const std::string NO_VALUE = "-";
        std::stringstream stream;
        if (!name.empty()) {
            stream << name << "=";
        }

        if (statistic.has_value()) {
            stream << std::fixed << std::setprecision(precision) << statistic.value();
        } else {
            stream << NO_VALUE;
        }

        return stream.str();
    }
};

template<typename T, std::enable_if_t<std::is_arithmetic<T>::value, int> = 0>
class FullAccumulator : public Accumulator<T>
{
public:
    // Creation isn't thread safe
    FullAccumulator(const std::string& data_type) :
        Accumulator<T>(data_type),
        m_lock(),
        m_count(0),
        m_min(static_cast<double>(std::numeric_limits<T>::max())),
        m_max(static_cast<double>(std::numeric_limits<T>::min())),
        m_mean(0),
        m_M2(0)
    {}
    FullAccumulator(FullAccumulator &&) = default;
    FullAccumulator(const FullAccumulator &) = delete;
    FullAccumulator &operator=(FullAccumulator &&) = delete;
    FullAccumulator &operator=(const FullAccumulator &) = delete;
    virtual ~FullAccumulator() = default;

    virtual void add_data_point(T data, uint32_t samples_count = 1) override
    {
        std::lock_guard<std::recursive_mutex> lock_guard(m_lock);
        m_min = std::min(m_min, static_cast<double>(data));
        m_max = std::max(m_max, static_cast<double>(data));
        m_count += samples_count;
        
        // mean, variance, sd and mean_sd are calculated using Welford's_online_algorithm.
        // See: https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance#Welford's_online_algorithm
        const auto delta = static_cast<double>(data) - m_mean;
        m_mean += ((delta * samples_count )/ static_cast<double>(m_count));
        m_M2 += delta * (static_cast<double>(data) - m_mean);
    }

    virtual AccumulatorResults get() const override
    {
        std::lock_guard<std::recursive_mutex> lock_guard(m_lock);
        return AccumulatorResults(count(), min(), max(), mean(), var(), sd(), mean_sd());
    }

    virtual AccumulatorResults get_and_clear() override
    {
        std::lock_guard<std::recursive_mutex> lock_guard(m_lock);
        auto result = get();
        clear();
        return result;
    }

    virtual Expected<size_t> count() const override
    {
        std::lock_guard<std::recursive_mutex> lock_guard(m_lock);
        return Expected<size_t>(m_count);
    }

    virtual Expected<double> min() const override
    {
        std::lock_guard<std::recursive_mutex> lock_guard(m_lock);
        if (m_count < 1) {
            return make_unexpected(HAILO_UNINITIALIZED);
        }
        return Expected<double>(m_min);
    }

    virtual Expected<double> max() const override
    {
        std::lock_guard<std::recursive_mutex> lock_guard(m_lock);
        if (m_count < 1) {
            return make_unexpected(HAILO_UNINITIALIZED);
        }
        return Expected<double>(m_max);
    }

    virtual Expected<double> mean() const override
    {
        std::lock_guard<std::recursive_mutex> lock_guard(m_lock);
        if (m_count < 1) {
            // Otherwise we'll divide by zero
            return make_unexpected(HAILO_UNINITIALIZED);
        }
        return Expected<double>(m_mean);
    }

    // Sample variance
    virtual Expected<double> var() const override
    {
        std::lock_guard<std::recursive_mutex> lock_guard(m_lock);
        if (m_count < 2) {
            // Otherwise we'll divide by zero
            return make_unexpected(HAILO_UNINITIALIZED);
        }
        return Expected<double>(var_impl());
    }

    // Sample sd
    virtual Expected<double> sd() const override
    {
        std::lock_guard<std::recursive_mutex> lock_guard(m_lock);
        if (m_count < 2) {
            // Otherwise we'll divide by zero
            return make_unexpected(HAILO_UNINITIALIZED);
        }
        return Expected<double>(sd_impl());
    }

    // Sample mean sd
    virtual Expected<double> mean_sd() const override
    {
        std::lock_guard<std::recursive_mutex> lock_guard(m_lock);
        if (m_count < 2) {
            // Otherwise we'll divide by zero
            return make_unexpected(HAILO_UNINITIALIZED);
        }
        // Calculation based on: https://en.wikipedia.org/wiki/Standard_deviation#Standard_deviation_of_the_mean
        return Expected<double>(sd_impl() / std::sqrt(m_count));
    }

protected:
    mutable std::recursive_mutex m_lock;
    size_t m_count;
    double m_min;
    double m_max;
    double m_mean;
    double m_M2; // Sum {i=1...n} (x_i-x_mean)^2

    // These functions are to be called after acquiring the mutex
    virtual void clear()
    {
        m_count = 0;
        m_min = static_cast<double>(std::numeric_limits<T>::max());
        m_max = static_cast<double>(std::numeric_limits<T>::min());
        m_mean = 0;
        m_M2 = 0;
    }

    double var_impl() const
    {
        return m_M2 / static_cast<double>(m_count - 1);
    }

    double sd_impl() const
    {
        return std::sqrt(var_impl());
    }
};

template<typename T, std::enable_if_t<std::is_arithmetic<T>::value, int> = 0>
class AverageFPSAccumulator : public FullAccumulator<T>
{
public:
    // Creation isn't thread safe
    AverageFPSAccumulator(const std::string& data_type) : 
        FullAccumulator<T>(data_type),
        m_sum(0)
    {}
    AverageFPSAccumulator(AverageFPSAccumulator &&) = default;
    AverageFPSAccumulator(const AverageFPSAccumulator &) = delete;
    AverageFPSAccumulator &operator=(AverageFPSAccumulator &&) = delete;
    AverageFPSAccumulator &operator=(const AverageFPSAccumulator &) = delete;
    virtual ~AverageFPSAccumulator() = default;


    // data is a duration of time.
    // However, the statistics collected will be in frames per seconds (i.e. time^-1).
    virtual void add_data_point(T data, uint32_t samples_count = 1) override
    {
        assert(0 != data);

        std::lock_guard<std::recursive_mutex> lock_guard(this->m_lock);
        m_sum += data;
        const double data_inverse = 1.0 / static_cast<double>(data);
        // Note: 'this' is needed to access protected members of a template base class
        this->m_min = std::min(this->m_min, data_inverse);
        this->m_max = std::max(this->m_max, data_inverse);
        this->m_count += samples_count;
        
        // mean, variance, sd and mean_sd are calculated using Welford's_online_algorithm.
        // See: https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance#Welford's_online_algorithm
        const auto delta = data_inverse - this->m_mean;
        // We calculate the arithmatic mean
        this->m_mean = static_cast<double>(this->m_count * samples_count) / static_cast<double>(m_sum);
        this->m_M2 += delta * (data_inverse - this->m_mean);
    }

    virtual void clear() override
    {
        m_sum = 0;
        FullAccumulator<T>::clear();
    }

private:
    T m_sum; // the sum of data added with add_data_point, before inversion
};

} /* namespace hailort */

#endif /* _HAILO_RUNTIME_STATISTICS_INTERNAL_HPP_ */
