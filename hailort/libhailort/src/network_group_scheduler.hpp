/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_group_scheduler.hpp
 * @brief Class declaration for NetworkGroupScheduler that schedules network groups to be active depending on the scheduling algorithm.
 **/

#ifndef _HAILO_NETWORK_GROUP_SCHEDULER_HPP_
#define _HAILO_NETWORK_GROUP_SCHEDULER_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "common/utils.hpp"
#include "hailo/network_group.hpp"

#include <condition_variable>

namespace hailort
{

class NetworkGroupScheduler;
using NetworkGroupSchedulerPtr = std::shared_ptr<NetworkGroupScheduler>;

// We use mostly weak pointer for the scheduler to prevent circular dependency of the pointers
using NetworkGroupSchedulerWeakPtr = std::weak_ptr<NetworkGroupScheduler>;

using network_group_name_t = std::string;
using stream_name_t = std::string;
class NetworkGroupScheduler final {
public:
    static Expected<NetworkGroupSchedulerPtr> create_shared(hailo_scheduling_algorithm_t algorithm);
    NetworkGroupScheduler(hailo_scheduling_algorithm_t algorithm)
        : m_algorithm(algorithm), m_before_read_write_mutex(), m_after_read_mutex(), m_cv(), m_current_network_group(0),
            m_is_current_ng_ready(false) {}

    hailo_scheduling_algorithm_t algorithm()
    {
        return m_algorithm;
    }

    hailo_status add_network_group(std::weak_ptr<ConfiguredNetworkGroup> added_cng);
    hailo_status wait_for_write(const std::string &network_group_name, const std::string &stream_name);
    hailo_status wait_for_read(const std::string &network_group_name, const std::string &stream_name);
    hailo_status signal_read_finish(const std::string &network_group_name, const std::string &stream_name);
    hailo_status enable_stream(const std::string &network_group_name, const std::string &stream_name);
    hailo_status disable_stream(const std::string &network_group_name, const std::string &stream_name);

private:
    hailo_status check_for_first_activation(const std::string &network_group_name);
    bool is_network_group_ready(const std::string &network_group_name);
    hailo_status switch_network_group_if_ready(const std::string &old_network_group_name);

    hailo_scheduling_algorithm_t m_algorithm;
    std::mutex m_before_read_write_mutex;
    std::mutex m_after_read_mutex;
    std::condition_variable m_cv;
    std::unordered_map<network_group_name_t, std::unordered_map<stream_name_t, std::atomic_bool>> m_should_ng_stop;
    std::unordered_map<network_group_name_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> m_got_write;
    std::unordered_map<network_group_name_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> m_got_read;
    std::unordered_map<network_group_name_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> m_finished_read;
    uint32_t m_current_network_group;
    std::vector<std::weak_ptr<ConfiguredNetworkGroup>> m_cngs;
    std::unordered_map<network_group_name_t, uint32_t> m_index_by_cng_name; // TODO: Remove this when moving the scheduler to a thread or service
    std::unordered_map<network_group_name_t, std::weak_ptr<ConfiguredNetworkGroup>> m_cngs_by_name;
    std::unordered_map<network_group_name_t, std::unordered_map<stream_name_t, uint16_t>> m_batch_size_per_stream;
    std::unique_ptr<ActivatedNetworkGroup> m_ang;
    bool m_is_current_ng_ready;
};

} /* namespace hailort */

#endif /* _HAILO_NETWORK_GROUP_SCHEDULER_HPP_ */
