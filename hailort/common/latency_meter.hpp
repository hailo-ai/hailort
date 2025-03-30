/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file latency_meter.hpp
 * @brief Calculate inference frame latency
 **/

#ifndef _HAILO_LATENCY_METER_HPP_
#define _HAILO_LATENCY_METER_HPP_

#include "hailo/expected.hpp"
#include "common/circular_buffer.hpp"

#include <set>
#include <mutex>
#include <unordered_map>

namespace hailort
{

/**
 * Used to measure latency of hailo datastream - the average amount of time between
 * the start of the action to the end of the last stream.
 */
class LatencyMeter final {
public:
    using duration = std::chrono::nanoseconds;
    using TimestampsArray = CircularArray<duration>;

    LatencyMeter(const std::set<std::string> &output_names, size_t timestamps_list_length) :
        m_start_timestamps(timestamps_list_length),
        m_latency_count(0),
        m_latency_sum(0)
    {
        for (auto &ch : output_names) {
            m_end_timestamps_per_channel.emplace(ch, TimestampsArray(timestamps_list_length));
        }
    }

    /**
     * Adds the given timestamp as a start.
     * @note Assumes it is the only thread that is calling the function
     */
    void add_start_sample(duration timestamp)
    {
        m_start_timestamps.push_back(timestamp);
        update_latency();
    }

    /*
     * Adds the given timestamp as the end of the given channel. The operation is done
     * after this function is called on all channels.
     * @note Assumes that only one thread per channel is calling this function.
     */  
    void add_end_sample(const std::string &stream_name, duration timestamp)
    {
        // Safe to access from several threads (when each pass different channel) because the map cannot
        // be changed in runtime.
        assert(m_end_timestamps_per_channel.find(stream_name) != m_end_timestamps_per_channel.end());
        m_end_timestamps_per_channel.at(stream_name).push_back(timestamp);
        update_latency();
    }

    /**
     * Queries average latency. One can clear measured latency by passing clear=true.
     */
    Expected<duration> get_latency(bool clear)
    {
        std::lock_guard<std::mutex> lock_guard(m_lock);

        if (m_latency_count == 0) {
            return make_unexpected(HAILO_NOT_AVAILABLE);
        }
        
        duration latency = (m_latency_sum / m_latency_count);
        if (clear) {
            m_latency_sum = duration();
            m_latency_count = 0;
        }

        return latency;
    }

private:
    void update_latency()
    {
        std::lock_guard<std::mutex> lock_guard(m_lock);

        if (m_start_timestamps.empty()) {
            // wait for begin sample
            return;
        }

        duration end(0);
        for (auto &end_timesatmps : m_end_timestamps_per_channel) {
            if (end_timesatmps.second.empty()) {
                // Wait for all channel samples
                return;
            }

            end = std::max(end, end_timesatmps.second.front());
        }

        duration start = m_start_timestamps.front();
        assert(start <= end);

        // calculate the latency
        m_latency_sum += (end - start);
        m_latency_count++;

        // pop fronts
        m_start_timestamps.pop_front();
        for (auto &end_timesatmps : m_end_timestamps_per_channel) {
            end_timesatmps.second.pop_front();
        }
    }

    std::mutex m_lock;

    TimestampsArray m_start_timestamps;
    std::unordered_map<std::string, TimestampsArray> m_end_timestamps_per_channel;

    size_t m_latency_count;
    duration m_latency_sum;
};

using LatencyMeterPtr = std::shared_ptr<LatencyMeter>;
using LatencyMetersMap = std::map<std::string, LatencyMeterPtr>;

} /* namespace hailort */

#endif /* _HAILO_LATENCY_METER_HPP_ */
