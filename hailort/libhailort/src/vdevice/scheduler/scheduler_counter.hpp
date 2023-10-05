/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file scheduler_counter.hpp
 * @brief Counter object that wraps a single counter per stream.
 **/

#ifndef _HAILO_SCHEDULER_COUNTER_HPP_
#define _HAILO_SCHEDULER_COUNTER_HPP_

#include "common/utils.hpp"

#include <unordered_map>
#include <cassert>
#include <atomic>

namespace hailort
{

using stream_name_t = std::string;

class SchedulerCounter
{
public:
    SchedulerCounter() : m_map()
    {}

    void insert(const stream_name_t &name)
    {
        assert(!contains(m_map, name));
        m_map[name] = 0;
    }

    uint32_t operator[](const stream_name_t &name) const
    {
        assert(contains(m_map, name));
        return m_map.at(name);
    }

    void increase(const stream_name_t &name)
    {
        assert(contains(m_map, name));
        m_map[name]++;
    }

    void decrease(const stream_name_t &name)
    {
        assert(contains(m_map, name));
        assert(m_map[name] > 0);
        m_map[name]--;
    }

    uint32_t get_min_value() const
    {
        return get_min_value_of_unordered_map(m_map);
    }

    uint32_t get_max_value() const
    {
        return get_max_value_of_unordered_map(m_map);
    }

    bool all_values_bigger_or_equal(uint32_t value) const
    {
        for (const auto &pair : m_map) {
            if (value > pair.second) {
                return false;
            }
        }
        return true;
    }

    bool empty() const
    {
        for (const auto &pair : m_map) {
            if (0 != pair.second) {
                return false;
            }
        }
        return true;
    }

    void reset()
    {
        for (auto &pair : m_map) {
            pair.second = 0;
        }
    }

private:
    std::unordered_map<stream_name_t, std::atomic_uint32_t> m_map;
};


} /* namespace hailort */

#endif /* _HAILO_SCHEDULER_COUNTER_HPP_ */
