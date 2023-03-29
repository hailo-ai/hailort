/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_group_scheduler.hpp
 * @brief Class declaration for CoreOpsScheduler that schedules core-ops to be active depending on the scheduling algorithm.
 **/

#ifndef _HAILO_NETWORK_GROUP_SCHEDULER_HPP_
#define _HAILO_NETWORK_GROUP_SCHEDULER_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include "common/utils.hpp"
#include "common/filesystem.hpp"

#include "vdevice/scheduler/scheduler_mon.hpp"
#include "vdevice/scheduler/scheduled_core_op_state.hpp"
#include "vdevice/scheduler/scheduled_core_op_cv.hpp"
#include "vdevice/scheduler/scheduler_base.hpp"


namespace hailort
{

#define INVALID_CORE_OP_HANDLE (UINT32_MAX)
#define INVALID_DEVICE_ID (UINT32_MAX)

using scheduler_core_op_handle_t = uint32_t;
using core_op_priority_t = uint8_t;
using scheduler_device_idx_t = uint32_t;

class CoreOpsScheduler;
using CoreOpsSchedulerPtr = std::shared_ptr<CoreOpsScheduler>;

// We use mostly weak pointer for the scheduler to prevent circular dependency of the pointers
using CoreOpsSchedulerWeakPtr = std::weak_ptr<CoreOpsScheduler>;

using stream_name_t = std::string;

class CoreOpsScheduler : public SchedulerBase
{
public:
    static Expected<CoreOpsSchedulerPtr> create_round_robin(uint32_t device_count, std::vector<std::string> &devices_bdf_id, 
        std::vector<std::string> &devices_arch);
    CoreOpsScheduler(hailo_scheduling_algorithm_t algorithm, uint32_t device_count, std::vector<std::string> &devices_bdf_id, 
        std::vector<std::string> &devices_arch);

    virtual ~CoreOpsScheduler();
    CoreOpsScheduler(const CoreOpsScheduler &other) = delete;
    CoreOpsScheduler &operator=(const CoreOpsScheduler &other) = delete;
    CoreOpsScheduler &operator=(CoreOpsScheduler &&other) = delete;
    CoreOpsScheduler(CoreOpsScheduler &&other) noexcept = delete;

    Expected<scheduler_core_op_handle_t> add_core_op(std::shared_ptr<CoreOp> added_core_op);

    hailo_status wait_for_write(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name,
        const std::chrono::milliseconds &timeout, const std::function<bool()> &should_cancel);
    hailo_status signal_write_finish(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name, bool did_write_fail);
    Expected<uint32_t> wait_for_read(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name,
        const std::chrono::milliseconds &timeout);
    hailo_status signal_read_finish(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name, uint32_t device_id);

    hailo_status enable_stream(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name);
    hailo_status disable_stream(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name);

    hailo_status set_timeout(const scheduler_core_op_handle_t &core_op_handle, const std::chrono::milliseconds &timeout, const std::string &network_name);
    hailo_status set_threshold(const scheduler_core_op_handle_t &core_op_handle, uint32_t threshold, const std::string &network_name);
    hailo_status set_priority(const scheduler_core_op_handle_t &core_op_handle, core_op_priority_t priority, const std::string &network_name);

    virtual ReadyInfo is_core_op_ready(const scheduler_core_op_handle_t &core_op_handle, bool check_threshold) override;
    virtual bool has_core_op_drained_everything(const scheduler_core_op_handle_t &core_op_handle, uint32_t device_id) override;

    void notify_all();

protected:
    bool choose_next_core_op(size_t device_id, bool check_threshold);

    std::unordered_map<scheduler_core_op_handle_t, std::atomic_bool> m_changing_current_batch_size;
    std::unordered_map<scheduler_core_op_handle_t, std::map<stream_name_t, std::atomic_bool>> m_should_core_op_stop;

private:
    hailo_status switch_core_op(const scheduler_core_op_handle_t &core_op_handle, uint32_t device_id,
        bool keep_nn_config = false);
    void reset_current_core_op_timestamps(uint32_t device_id);

    hailo_status send_all_pending_buffers(const scheduler_core_op_handle_t &core_op_handle, uint32_t device_id, uint32_t burst_size);
    hailo_status send_pending_buffer(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name, uint32_t device_id);

    void decrease_core_op_counters(const scheduler_core_op_handle_t &core_op_handle);
    bool should_core_op_stop(const scheduler_core_op_handle_t &core_op_handle);
    bool core_op_all_streams_aborted(const scheduler_core_op_handle_t &core_op_handle);

    std::string get_core_op_name(const scheduler_core_op_handle_t &core_op_handle);
    bool is_core_op_active(const scheduler_core_op_handle_t &core_op_handle);
    bool is_multi_device();
    hailo_status optimize_streaming_if_enabled(const scheduler_core_op_handle_t &network_group_handle);
    uint16_t get_min_avail_buffers_count(const scheduler_core_op_handle_t &network_group_handle, uint32_t device_id);

    hailo_status start_mon();
    void time_dependent_events_cycle_calc();
    void log_monitor_device_infos(ProtoMon &mon);
    void log_monitor_networks_infos(ProtoMon &mon);
    void log_monitor_frames_infos(ProtoMon &mon);
    void update_utilization_timers(scheduler_device_idx_t device_id, scheduler_core_op_handle_t core_op_handle);
    void update_utilization_timestamp(scheduler_device_idx_t device_id);
    void update_utilization_send_started(scheduler_device_idx_t device_id);
    void update_device_drained_state(scheduler_device_idx_t device_id, bool state);
    void update_utilization_read_buffers_finished(scheduler_device_idx_t device_id, scheduler_core_op_handle_t core_op_hanle, bool is_drained_everything);
    hailo_status set_h2d_frames_counters(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name,
        ProtoMonStreamFramesInfo &stream_frames_info);
    hailo_status set_d2h_frames_counters(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name,
        ProtoMonStreamFramesInfo &stream_frames_info);
#if defined(__GNUC__)
    Expected<std::shared_ptr<TempFile>> open_temp_mon_file();
    void dump_state();
#endif

    std::vector<std::shared_ptr<ScheduledCoreOp>> m_scheduled_core_ops;
    std::mutex m_before_read_write_mutex;
    std::unordered_map<scheduler_core_op_handle_t, std::shared_ptr<ScheduledCoreOpCV>> m_core_ops_cvs;

    // Params for the scheduler MON
    std::atomic_bool m_should_monitor;
    std::thread m_mon_thread;
    EventPtr m_mon_shutdown_event;
#if defined(__GNUC__)
    std::shared_ptr<TempFile> m_mon_tmp_output;
#endif
    std::chrono::time_point<std::chrono::steady_clock> m_last_measured_timestamp;
    double m_last_measured_time_duration;
    std::unordered_map<scheduler_device_idx_t, double> m_device_utilization;
    std::unordered_map<scheduler_device_idx_t, std::atomic_bool> m_device_has_drained_everything;
    std::unordered_map<scheduler_device_idx_t, std::chrono::time_point<std::chrono::steady_clock>> m_last_measured_utilization_timestamp;
    // TODO: Consider adding Accumulator classes for more info (min, max, mean, etc..)
    std::unordered_map<scheduler_core_op_handle_t, double> m_core_op_utilization;
    std::unordered_map<scheduler_core_op_handle_t, std::atomic_uint32_t> m_fps_accumulator;
};

} /* namespace hailort */

#endif /* _HAILO_NETWORK_GROUP_SCHEDULER_HPP_ */
