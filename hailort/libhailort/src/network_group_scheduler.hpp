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

#define DEFAULT_SCHEDULER_TIMEOUT (std::chrono::milliseconds(0))
#define DEFAULT_SCHEDULER_MIN_THRESHOLD (1)

namespace hailort
{

#define INVALID_NETWORK_GROUP_HANDLE (UINT32_MAX)
using network_group_handle_t = uint32_t;

const auto SCHEDULER_REFRESH_INTERVAL = std::chrono::milliseconds(500);

class NetworkGroupScheduler;
using NetworkGroupSchedulerPtr = std::shared_ptr<NetworkGroupScheduler>;

// We use mostly weak pointer for the scheduler to prevent circular dependency of the pointers
using NetworkGroupSchedulerWeakPtr = std::weak_ptr<NetworkGroupScheduler>;

using stream_name_t = std::string;
class NetworkGroupScheduler final {
public:
    static Expected<NetworkGroupSchedulerPtr> create_shared(hailo_scheduling_algorithm_t algorithm);
    NetworkGroupScheduler(hailo_scheduling_algorithm_t algorithm)
        : m_algorithm(algorithm), m_before_read_write_mutex(), m_write_read_cv(), m_current_network_group(0),
            m_switching_network_group(true), m_has_current_ng_finished(true), m_next_network_group(0), m_forced_idle_state(false)
        {
            // Temp solution until we'll implement timeout as timers
            m_thread = std::thread([this] () {
                while (!m_should_stop.load()) {
                    m_write_read_cv.notify_all();
                    std::this_thread::sleep_for(SCHEDULER_REFRESH_INTERVAL);
                }
          });
        }

    ~NetworkGroupScheduler()
    {
        m_should_stop = true;
        m_thread.join();
    }

    hailo_scheduling_algorithm_t algorithm()
    {
        return m_algorithm;
    }

    Expected<network_group_handle_t> add_network_group(std::weak_ptr<ConfiguredNetworkGroup> added_cng);

    hailo_status wait_for_write(const network_group_handle_t &network_group_handle, const std::string &stream_name);
    hailo_status signal_write_finish(const network_group_handle_t &network_group_handle, const std::string &stream_name);
    hailo_status wait_for_read(const network_group_handle_t &network_group_handle, const std::string &stream_name);
    hailo_status signal_read_finish(const network_group_handle_t &network_group_handle, const std::string &stream_name);

    hailo_status enable_stream(const network_group_handle_t &network_group_handle, const std::string &stream_name);
    hailo_status disable_stream(const network_group_handle_t &network_group_handle, const std::string &stream_name);

    hailo_status set_timeout(const network_group_handle_t &network_group_handle, const std::chrono::milliseconds &timeout, const std::string &network_name);
    hailo_status set_threshold(const network_group_handle_t &network_group_handle, uint32_t threshold, const std::string &network_name);

    class SchedulerIdleGuard final {
        /* After 'set_scheduler()' is called, the idle guard will guarantee nothing is running on the device.
        Relevant for state and configs changes */
    public:
        SchedulerIdleGuard() = default;
        ~SchedulerIdleGuard();
        hailo_status set_scheduler(std::shared_ptr<NetworkGroupScheduler> scheduler);
    private:
        std::shared_ptr<NetworkGroupScheduler> m_scheduler;
    };

    static std::unique_ptr<SchedulerIdleGuard> create_scheduler_idle_guard()
    {
        auto guard = make_unique_nothrow<SchedulerIdleGuard>();
        return guard;
    }

private:
    hailo_status switch_network_group_if_idle(const network_group_handle_t &network_group_handle, std::unique_lock<std::mutex> &read_write_lock);
    hailo_status activate_network_group(const network_group_handle_t &network_group_handle, std::unique_lock<std::mutex> &read_write_lock);
    hailo_status send_all_pending_buffers(const network_group_handle_t &network_group_handle, std::unique_lock<std::mutex> &read_write_lock);
    hailo_status send_pending_buffer(const network_group_handle_t &network_group_handle, const std::string &stream_name, std::unique_lock<std::mutex> &read_write_lock);
    hailo_status switch_network_group_if_should_be_next(const network_group_handle_t &network_group_handle, std::unique_lock<std::mutex> &read_write_lock);
    void deactivate_network_group();
    bool is_network_group_ready(const network_group_handle_t &network_group_handle);
    hailo_status mark_switching_ng_if_ready();
    bool can_stream_read(const network_group_handle_t &network_group_handle, const std::string &stream_name);
    bool has_current_ng_finished();
    void reset_current_ng_counters();
    void decrease_current_ng_counters();
    hailo_status allow_all_writes();
    hailo_status allow_writes_for_other_inputs_if_needed(const network_group_handle_t &network_group_handle);

    void force_idle_state();
    void resume_from_idle_state();

    hailo_scheduling_algorithm_t m_algorithm;
    std::mutex m_before_read_write_mutex;
    std::condition_variable m_write_read_cv;
    std::unordered_map<network_group_handle_t, std::unordered_map<stream_name_t, std::atomic_bool>> m_should_ng_stop;
    std::unordered_map<network_group_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> m_requested_write;
    std::unordered_map<network_group_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> m_written_buffer;
    std::unordered_map<network_group_handle_t, std::unordered_map<stream_name_t, EventPtr>> m_write_buffer_events;
    std::unordered_map<network_group_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> m_sent_pending_buffer;
    std::unordered_map<network_group_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> m_finished_sent_pending_buffer;
    std::unordered_map<network_group_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> m_allowed_read;
    std::unordered_map<network_group_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> m_finished_read;
    network_group_handle_t m_current_network_group;
    std::vector<std::weak_ptr<ConfiguredNetworkGroup>> m_cngs;
    std::unique_ptr<ActivatedNetworkGroup> m_ang;
    std::atomic_bool m_switching_network_group;
    std::atomic_bool m_has_current_ng_finished;
    network_group_handle_t m_next_network_group;
    std::atomic_bool m_forced_idle_state;

    std::thread m_thread;
    std::atomic_bool m_should_stop;

    std::unordered_map<network_group_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> m_min_threshold_per_stream;
    std::unordered_map<network_group_handle_t, std::chrono::milliseconds> m_timeout_per_network_group;
    std::unordered_map<network_group_handle_t, std::chrono::time_point<std::chrono::steady_clock>> m_last_run_time_stamp;
};

} /* namespace hailort */

#endif /* _HAILO_NETWORK_GROUP_SCHEDULER_HPP_ */
