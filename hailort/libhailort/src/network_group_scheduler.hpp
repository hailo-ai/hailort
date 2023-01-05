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
#include "scheduled_network_group.hpp"

#include <condition_variable>

#define DEFAULT_SCHEDULER_TIMEOUT (std::chrono::milliseconds(0))
#define DEFAULT_SCHEDULER_MIN_THRESHOLD (0)

namespace hailort
{

#define INVALID_NETWORK_GROUP_HANDLE (UINT32_MAX)
#define INVALID_DEVICE_ID (UINT32_MAX)

using scheduler_ng_handle_t = uint32_t;

class NetworkGroupScheduler;
using NetworkGroupSchedulerPtr = std::shared_ptr<NetworkGroupScheduler>;

// We use mostly weak pointer for the scheduler to prevent circular dependency of the pointers
using NetworkGroupSchedulerWeakPtr = std::weak_ptr<NetworkGroupScheduler>;

using stream_name_t = std::string;

struct ActiveDeviceInfo {
    ActiveDeviceInfo(uint32_t device_id) : current_network_group_handle(INVALID_NETWORK_GROUP_HANDLE),
        next_network_group_handle(INVALID_NETWORK_GROUP_HANDLE), is_switching_network_group(false), current_batch_size(0), current_burst_size(0),
        current_cycle_requested_transferred_frames_h2d(), current_cycle_finished_transferred_frames_d2h(), current_cycle_finished_read_frames_d2h(),
        device_id(device_id)
    {}
    scheduler_ng_handle_t current_network_group_handle;
    scheduler_ng_handle_t next_network_group_handle;
    std::atomic_bool is_switching_network_group;
    std::atomic_uint32_t current_batch_size;
    std::atomic_uint32_t current_burst_size;
    std::unordered_map<scheduler_ng_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> current_cycle_requested_transferred_frames_h2d;
    std::unordered_map<scheduler_ng_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> current_cycle_finished_transferred_frames_d2h;
    std::unordered_map<scheduler_ng_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> current_cycle_finished_read_frames_d2h;
    uint32_t device_id;
};

class NetworkGroupScheduler
{
public:
    static Expected<NetworkGroupSchedulerPtr> create_round_robin(uint32_t device_count);
    NetworkGroupScheduler(hailo_scheduling_algorithm_t algorithm, uint32_t device_count);

    virtual ~NetworkGroupScheduler();
    NetworkGroupScheduler(const NetworkGroupScheduler &other) = delete;
    NetworkGroupScheduler &operator=(const NetworkGroupScheduler &other) = delete;
    NetworkGroupScheduler &operator=(NetworkGroupScheduler &&other) = delete;
    NetworkGroupScheduler(NetworkGroupScheduler &&other) noexcept = delete;

    hailo_scheduling_algorithm_t algorithm()
    {
        return m_algorithm;
    }

    Expected<scheduler_ng_handle_t> add_network_group(std::shared_ptr<ConfiguredNetworkGroup> added_cng);

    hailo_status wait_for_write(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name,
        const std::chrono::milliseconds &timeout, const std::function<bool()> &should_cancel);
    hailo_status signal_write_finish(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name);
    Expected<uint32_t> wait_for_read(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name,
        const std::chrono::milliseconds &timeout);
    hailo_status signal_read_finish(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name, uint32_t device_id);

    hailo_status enable_stream(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name);
    hailo_status disable_stream(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name);

    hailo_status set_timeout(const scheduler_ng_handle_t &network_group_handle, const std::chrono::milliseconds &timeout, const std::string &network_name);
    hailo_status set_threshold(const scheduler_ng_handle_t &network_group_handle, uint32_t threshold, const std::string &network_name);

    void notify_all();
    void mark_failed_write(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name);

protected:
    struct ReadyInfo {
        bool threshold = false;
        bool timeout = false;
        bool is_ready = false;
    };

    void choose_next_network_group(size_t device_id);
    ReadyInfo is_network_group_ready(const scheduler_ng_handle_t &network_group_handle, bool check_threshold, uint32_t device_id);

    std::vector<std::shared_ptr<ActiveDeviceInfo>> m_devices;
    std::unordered_map<scheduler_ng_handle_t, std::atomic_bool> m_changing_current_batch_size;
    std::unordered_map<scheduler_ng_handle_t, std::map<stream_name_t, std::atomic_bool>> m_should_ng_stop;

    std::vector<std::shared_ptr<ScheduledNetworkGroup>> m_cngs;

private:
    hailo_status switch_network_group(const scheduler_ng_handle_t &network_group_handle, uint32_t device_id,
        bool keep_nn_config = false);
    void reset_current_ng_timestamps(uint32_t device_id);

    Expected<bool> should_wait_for_write(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name);
    hailo_status send_all_pending_buffers(const scheduler_ng_handle_t &network_group_handle, uint32_t device_id);
    hailo_status send_pending_buffer(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name, uint32_t device_id);
    
    void decrease_ng_counters(const scheduler_ng_handle_t &network_group_handle);
    bool has_ng_drained_everything(const scheduler_ng_handle_t &network_group_handle, uint32_t device_id);
    bool has_ng_finished(const scheduler_ng_handle_t &network_group_handle, uint32_t device_id);
    bool should_ng_stop(const scheduler_ng_handle_t &network_group_handle);
    bool ng_all_streams_aborted(const scheduler_ng_handle_t &network_group_handle);

    std::string get_network_group_name(const scheduler_ng_handle_t &network_group_handle);
    bool is_network_group_active(const scheduler_ng_handle_t &network_group_handle);
    bool is_switching_current_network_group(const scheduler_ng_handle_t &network_group_handle);
    bool is_multi_device();

    hailo_status start_mon();
    void log_monitor_networks_infos(ProtoMon &mon);
    void log_monitor_frames_infos(ProtoMon &mon);
    hailo_status set_h2d_frames_counters(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name,
        ProtoMonStreamFramesInfo &stream_frames_info);
    hailo_status set_d2h_frames_counters(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name,
        ProtoMonStreamFramesInfo &stream_frames_info);
#if defined(__GNUC__)
    Expected<std::shared_ptr<TempFile>> open_temp_mon_file();
    void dump_state();
#endif

    hailo_scheduling_algorithm_t m_algorithm;
    std::mutex m_before_read_write_mutex;
    std::condition_variable m_write_read_cv;
    scheduler_ng_handle_t m_last_choosen_network_group;

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

    friend class NetworkGroupSchedulerOracle;
};

} /* namespace hailort */

#endif /* _HAILO_NETWORK_GROUP_SCHEDULER_HPP_ */
