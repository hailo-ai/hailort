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
#include "hailo/network_group.hpp"
#include "common/utils.hpp"
#include "common/filesystem.hpp"
#include "scheduler_mon.hpp"

#include <condition_variable>

#define DEFAULT_SCHEDULER_TIMEOUT (std::chrono::milliseconds(0))
#define DEFAULT_SCHEDULER_MIN_THRESHOLD (1)

namespace hailort
{

#define INVALID_NETWORK_GROUP_HANDLE (UINT32_MAX)
using scheduler_ng_handle_t = uint32_t;

class NetworkGroupScheduler;
using NetworkGroupSchedulerPtr = std::shared_ptr<NetworkGroupScheduler>;

// We use mostly weak pointer for the scheduler to prevent circular dependency of the pointers
using NetworkGroupSchedulerWeakPtr = std::weak_ptr<NetworkGroupScheduler>;

using stream_name_t = std::string;


class NetworkGroupScheduler
{
public:
    static Expected<NetworkGroupSchedulerPtr> create_round_robin();
    NetworkGroupScheduler(hailo_scheduling_algorithm_t algorithm);

    virtual ~NetworkGroupScheduler();
    NetworkGroupScheduler(const NetworkGroupScheduler &other) = delete;
    NetworkGroupScheduler &operator=(const NetworkGroupScheduler &other) = delete;
    NetworkGroupScheduler &operator=(NetworkGroupScheduler &&other) = delete;
    NetworkGroupScheduler(NetworkGroupScheduler &&other) noexcept = delete;

    hailo_scheduling_algorithm_t algorithm()
    {
        return m_algorithm;
    }

    Expected<scheduler_ng_handle_t> add_network_group(std::weak_ptr<ConfiguredNetworkGroup> added_cng);

    hailo_status wait_for_write(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name);
    hailo_status signal_write_finish(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name);
    hailo_status wait_for_read(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name);
    hailo_status signal_read_finish(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name);

    hailo_status enable_stream(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name);
    hailo_status disable_stream(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name);

    hailo_status set_timeout(const scheduler_ng_handle_t &network_group_handle, const std::chrono::milliseconds &timeout, const std::string &network_name);
    hailo_status set_threshold(const scheduler_ng_handle_t &network_group_handle, uint32_t threshold, const std::string &network_name);

protected:
    hailo_status choose_next_network_group();
    virtual bool find_next_network_group_by_algorithm(bool check_threshold) = 0;
    bool is_network_group_ready(const scheduler_ng_handle_t &network_group_handle, bool check_threshold);

    std::atomic_bool m_is_switching_network_group;
    scheduler_ng_handle_t m_current_network_group;
    scheduler_ng_handle_t m_next_network_group;

    std::vector<std::weak_ptr<ConfiguredNetworkGroup>> m_cngs;
    std::unique_ptr<ActivatedNetworkGroup> m_ang;

private:
    hailo_status switch_network_group_if_should_be_next(const scheduler_ng_handle_t &network_group_handle, std::unique_lock<std::mutex> &read_write_lock);
    hailo_status switch_network_group_if_idle(const scheduler_ng_handle_t &network_group_handle, std::unique_lock<std::mutex> &read_write_lock);
    hailo_status activate_network_group(const scheduler_ng_handle_t &network_group_handle, std::unique_lock<std::mutex> &read_write_lock,
        bool keep_nn_config = false);
    void deactivate_network_group();
    void reset_current_ng_timestamps();
    hailo_status try_change_multicontext_ng_batch_size(const scheduler_ng_handle_t &network_group_handle, std::unique_lock<std::mutex> &read_write_lock);

    bool has_input_written_most_frames(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name);
    bool can_stream_write(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name);
    hailo_status block_write_if_needed(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name);
    Expected<bool> should_wait_for_write(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name);
    hailo_status allow_all_writes();
    hailo_status allow_writes_for_other_inputs_if_needed(const scheduler_ng_handle_t &network_group_handle);
    hailo_status send_all_pending_buffers(const scheduler_ng_handle_t &network_group_handle, std::unique_lock<std::mutex> &read_write_lock);
    hailo_status send_pending_buffer(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name, std::unique_lock<std::mutex> &read_write_lock);

    bool should_ng_stop(const scheduler_ng_handle_t &network_group_handle);
    bool can_stream_read(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name);
    bool has_ng_finished(scheduler_ng_handle_t network_group_handle);
    bool has_ng_drained_everything(scheduler_ng_handle_t network_group_handle);
    bool has_pending_frames(const scheduler_ng_handle_t &network_group_handle);
    bool has_enough_space_in_read_buffers(const scheduler_ng_handle_t &network_group_handle, uint32_t ongoing_frames);
    void decrease_current_ng_counters();
    bool is_ng_multicontext(const scheduler_ng_handle_t &network_group_handle);
    uint32_t get_buffered_frames_count();

    std::string get_network_group_name(scheduler_ng_handle_t network_group_handle);
    hailo_status start_mon();
    void log_monitor_networks_infos(ProtoMon &mon);
    void log_monitor_frames_infos(ProtoMon &mon);
    hailo_status set_h2d_frames_counters(scheduler_ng_handle_t network_group_handle, const std::string &stream_name,
        ProtoMonStreamFramesInfo &stream_frames_info);
    hailo_status set_d2h_frames_counters(scheduler_ng_handle_t network_group_handle, const std::string &stream_name,
        ProtoMonStreamFramesInfo &stream_frames_info);
#if defined(__GNUC__)
    Expected<std::shared_ptr<TempFile>> open_temp_mon_file();
    void dump_state();
#endif

    hailo_scheduling_algorithm_t m_algorithm;
    std::mutex m_before_read_write_mutex;
    std::atomic_uint32_t m_current_batch_size;
    std::condition_variable m_write_read_cv;

    std::unordered_map<scheduler_ng_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> m_requested_write;
    std::unordered_map<scheduler_ng_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> m_written_buffer;
    std::unordered_map<scheduler_ng_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> m_sent_pending_buffer;
    std::unordered_map<scheduler_ng_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> m_current_sent_pending_buffer;
    std::unordered_map<scheduler_ng_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> m_finished_sent_pending_buffer;

    std::unordered_map<scheduler_ng_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> m_requested_read;
    std::unordered_map<scheduler_ng_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> m_allowed_read;
    std::unordered_map<scheduler_ng_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> m_finished_read;
    std::unordered_map<scheduler_ng_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> m_current_finished_read;
    std::unordered_map<scheduler_ng_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> m_pending_read;

    std::unordered_map<scheduler_ng_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> m_min_threshold_per_stream;

    std::unordered_map<scheduler_ng_handle_t, std::chrono::time_point<std::chrono::steady_clock>> m_last_run_time_stamp;
    std::unordered_map<scheduler_ng_handle_t, std::shared_ptr<std::chrono::milliseconds>> m_timeout_per_network_group;
    std::unordered_map<scheduler_ng_handle_t, std::atomic_bool> m_frame_was_sent_per_network_group;
    std::unordered_map<scheduler_ng_handle_t, uint16_t> m_max_batch_size;

    std::unordered_map<scheduler_ng_handle_t, std::unordered_map<stream_name_t, std::atomic_bool>> m_should_ng_stop;
    std::unordered_map<scheduler_ng_handle_t, std::unordered_map<stream_name_t, EventPtr>> m_write_buffer_events;

    // Params for the scheduler MON
    std::atomic_bool m_should_monitor;
    std::thread m_mon_thread;
    EventPtr m_mon_shutdown_event;
#if defined(__GNUC__)
    std::shared_ptr<TempFile> m_mon_tmp_output;
#endif
    std::chrono::time_point<std::chrono::steady_clock> m_last_measured_timestamp;
    std::unordered_map<scheduler_ng_handle_t, std::chrono::time_point<std::chrono::steady_clock>> m_last_measured_activation_timestamp; 
    // TODO: Consider adding Accumulator classes for more info (min, max, mean, etc..)
    std::unordered_map<scheduler_ng_handle_t, double> m_active_duration;
    std::unordered_map<scheduler_ng_handle_t, std::atomic_uint32_t> m_fps_accumulator;
};


class NetworkGroupSchedulerRoundRobin : public NetworkGroupScheduler
{
public:
    NetworkGroupSchedulerRoundRobin() :
        NetworkGroupScheduler(HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN)
        {};

protected:
    virtual bool find_next_network_group_by_algorithm(bool check_threshold) override;
};

} /* namespace hailort */

#endif /* _HAILO_NETWORK_GROUP_SCHEDULER_HPP_ */
